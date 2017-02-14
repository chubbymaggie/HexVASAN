#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#ifndef NO_BACKTRACE
#include <execinfo.h>
#endif
#include "vasan.h"
#include "stack.h"
#include "hashmap.h"

#ifdef __FreeBSD__
# include <sys/thr.h> // for thr_self
#else
# include <unistd.h>  // for syscall
# ifdef __x86_64__
#  include <asm/unistd_64.h> // for __NR_gettid
# else
#  include <asm/unistd_32.h>
# endif
#endif

#define DEBUG
#define MAXPATH 1000

// Linked list that holds call site info that is not assigned to va_lists yet.
// This replaces the vasan_stack.
struct vasan_type_list_elem
{
    // id for the thread that inserted this elem
    int tid;
	unsigned char consumed;
    struct vasan_type_info_tmp* info;
    struct vasan_type_list_elem* next;
    struct vasan_type_list_elem* prev;
};

struct vasan_global_state
{
	// only holds the call site info
    struct vasan_type_list_elem* vasan_list;

	// maps va_lists onto their call site info
    map_t vasan_map;

	// for statistics only
	map_t callsite_cnt;
	map_t vfunc_cnt;

	// protects accesses to global structures
	// it's just a simple spinlock as we don't
	// expect much contention
	volatile int spinlock;

	// if set to 1, we log violations but don't terminate
	// the program when a violation is triggered
	unsigned char logging_only;

	// file we're logging to. can be either stderr or a real file
	FILE* fp;
};

// Every lib/binary we're linking vasan into will have a vasan_global_state
// struct, but we ONLY use the vasan_global_state of ONE of the libs/binaries
//
// We do this by calculating the address of the global state we're going to
// use using dlsym. 
__attribute__((visibility("default"))) struct vasan_global_state _vasan_global =
{ (struct vasan_type_list_elem*)0, (map_t)0, (map_t)0, (map_t)0, 0, 0, (FILE*)0 };

// This pointer points at the global state we're using.
// It might or might not be in the same lib/binary in which this pointer
// is declared (see above).
static struct vasan_global_state* vasan_global = 0;

// When set to 1, it's safe to access the global state
static unsigned char vasan_initialized = 0;

// cross-platform gettid func
static int __vasan_gettid(void)
{
	int ret;

#if defined(__FreeBSD__)
	long long_tid;
    thr_self( &long_tid );
    ret = long_tid;
#elif defined(__x86_64__)
	// can't use the syscall macro here because it's also a vararg func
	unsigned long long_ret;
	__asm__ __volatile__ ("syscall" : "=a"(long_ret) : "a"(__NR_gettid) : "rcx", "r11", "memory");
	ret = long_ret;
#else
	#error "i386 support is not implemented yet"
#endif

    return ret;
}

static void __vasan_lock()
{
	while(!__sync_bool_compare_and_swap(&vasan_global->spinlock, 0, 1))
		asm volatile("rep; nop" ::: "memory");
}

static void __vasan_unlock()
{
	asm volatile("" ::: "memory");
	vasan_global->spinlock = 0;
}

static void __vasan_backtrace()
{
#ifndef NO_BACKTRACE
    void *trace[16];
	char **messages = (char **)NULL;
	int i, trace_size = 0;

	trace_size = backtrace(trace, 16);
	messages = backtrace_symbols(trace, trace_size);
	(fprintf)(vasan_global->fp, "Backtrace:\n");
	for (i=0; i<trace_size; ++i)
		(fprintf)(vasan_global->fp, "[%d] %s\n", i, messages[i]);
	free(messages);
#endif
}

static struct vasan_type_list_elem* __vasan_list_elem_new()
{
	struct vasan_type_list_elem* list = (struct vasan_type_list_elem*)malloc(sizeof(struct vasan_type_list_elem));
	list->tid  = __vasan_gettid();
	list->consumed = 0;
	list->info = (struct vasan_type_info_tmp*)0;
	list->next = list->prev = (struct vasan_type_list_elem*)0;
	return list;
}

static void __vasan_list_insert(struct vasan_type_info_tmp* info)
{
	struct vasan_type_list_elem* elem = __vasan_list_elem_new();
	elem->info = info;

	__vasan_lock();	
		
	elem->next = vasan_global->vasan_list->next;
	elem->prev = vasan_global->vasan_list;
	if (elem->next)
		elem->next->prev = elem;
	vasan_global->vasan_list->next = elem;
	__vasan_unlock();
}

// fetches the latest element inserted by this thread
static struct vasan_type_list_elem* __vasan_list_get(unsigned char fetch_consumed)
{
	int tid = __vasan_gettid();

	__vasan_lock();
	for (struct vasan_type_list_elem* tmp = vasan_global->vasan_list->next;
		 tmp; tmp = tmp->next)
	{
		if (tmp->tid == tid)
		{
			// don't return the element if it has already been consumed.
			// that means it's just waiting to be cleaned up
			if (tmp->consumed && !fetch_consumed)
				break;
			__vasan_unlock();
			return tmp;
		}
	}

	__vasan_unlock();
	return (struct vasan_type_list_elem*)0;
}

static void __vasan_list_unlink_and_free(struct vasan_type_list_elem* elem)
{
	__vasan_lock();
	if (elem->next)
		elem->next->prev = elem->prev;
	elem->prev->next = elem->next;
	__vasan_unlock();
	free(elem);
}

// We have to refuse to initialize until TLS is active
static unsigned char __vasan_init()
{
	char path[MAXPATH];
	
	// make sure everyone uses the same global state
	vasan_global = dlsym(RTLD_DEFAULT, "_vasan_global");

	// assume dlsym() returns null only when it is staic link
	if (!vasan_global)
		vasan_global = &_vasan_global;

	vasan_initialized = 1;

	FILE *fp = NULL;
	char *home = getenv("VASAN_ERR_LOG_PATH");

	if (home != 0)
	{
		strcpy(path, home);
		strcat(path, "error.txt");

		// open log file and remember the fp
		fp = fopen(path, "a+");
	}

	// make global state init thread safe
	__vasan_lock();
	
	if (vasan_global->vasan_list)
	{
		__vasan_unlock();
		return 1;
	}

	vasan_global->vasan_list = __vasan_list_elem_new();
	vasan_global->vasan_map = __vasan_hashmap_new();

	if (fp)
	{
		// Only track statistics if we're in logging mode
		vasan_global->callsite_cnt = __vasan_hashmap_new();
		vasan_global->vfunc_cnt = __vasan_hashmap_new();

		// remember the fp
		vasan_global->fp = fp;
		vasan_global->logging_only = 1;
	}

	if (!vasan_global->fp)
		vasan_global->fp = stderr;

	__vasan_unlock();
    return 1;
}

void __attribute__((destructor)) __vasan_fini()
{
	// TODO: Do a proper cleanup here
	
	/*
	stack_free(vasan_global->vasan_stack);
	if (vasan_global->callsite_cnt)
	hashmap_free(vasan_global->callsite_cnt);
	if (vasan_global->vfunc_cnt)
	hashmap_free(vasan_global->vfunc_cnt);
	*/

	if (vasan_global && vasan_global->fp && vasan_global->fp != stderr)
	{
		fclose(vasan_global->fp);
		vasan_global->fp = (FILE*)0;
	}
}

// CallerSide: Function to push the pointer in the stack
void
__vasan_info_push(struct vasan_type_info_tmp *x)
{
	if (!vasan_initialized)
		__vasan_init();

	// hack for fopen() calling variadic function during __vasan_init().
	// FIXME: it's possible that this get lock() faster than __vasan_init()...
	__vasan_lock();
	if (!vasan_global->vasan_list)
	{
		__vasan_unlock();
		return;
	}
	__vasan_unlock();

	if (vasan_global->callsite_cnt)
	{
		__vasan_lock();
		int* cnt;
		if (__vasan_hashmap_get(vasan_global->callsite_cnt, x->id, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			__vasan_hashmap_put(vasan_global->callsite_cnt, x->id, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
		__vasan_unlock();
	}

	__vasan_list_insert(x);
}

// We've seen a va_start call.
// Associate the corresponding vasan_type_info_tmp struct with this list
// and store it in the vasan map
void
__vasan_vastart(va_list* list)
{
	if (!vasan_initialized)
		__vasan_init();

	struct vasan_type_list_elem* latest = __vasan_list_get(0);

	if (!latest)
		return;

	__vasan_lock();

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
	{
		info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_global->vasan_map, (unsigned long)list, (any_t*)info);
	}
	
	info->list_ptr = list;
	info->args_ptr = 0;
	info->types = latest->info;
	latest->consumed = 1;

	__vasan_unlock();
}

// This list is no longer going to be used.
// Remove it from the vasan map
void
__vasan_vaend(va_list* list)
{
	if (!vasan_initialized)
		__vasan_init();

	__vasan_lock();
	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) != MAP_MISSING)
	{
		__vasan_hashmap_remove(vasan_global->vasan_map, (unsigned long)list);
		__vasan_unlock();
		free(info);
	}
	else
	{
		__vasan_unlock();
	}
}

// Create a copy of another list IN ITS CURRENT STATE!
void
__vasan_vacopy(va_list* src, va_list* dst)
{
	if (!vasan_initialized)
		__vasan_init();

	__vasan_lock();
	struct vasan_type_info_full* src_info, *dst_info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)src, (any_t*)&src_info) == MAP_MISSING)
	{
		__vasan_unlock();
		return;
	}
	
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)dst, (any_t*)&dst_info) == MAP_MISSING)
	{
		dst_info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_global->vasan_map, (unsigned long)dst, (any_t*)dst_info);
	}

	dst_info->list_ptr = dst;
	dst_info->args_ptr = src_info->args_ptr;
	dst_info->types = src_info->types;
	__vasan_unlock();
}

// CallerSide: Function to pop the pointer from the stack
void
__vasan_info_pop(int i)
{
	if (!vasan_initialized)
		__vasan_init();

	// hack for fopen() calling variadic function during __vasan_init().
	__vasan_lock();
	if (!vasan_global->vasan_list)
	{
		__vasan_unlock();
		return;
	}
	__vasan_unlock();

	__vasan_list_unlink_and_free(__vasan_list_get(1));
}


// New version of the check_index function. You no longer have to figure out
// the index for this one. You just need a list pointer...
void
__vasan_check_index_new(va_list* list, unsigned long type)
{
	if (!vasan_initialized)
		__vasan_init();

	__vasan_lock();
	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
	{
		__vasan_unlock();
		return;
	}
	else
	{
		__vasan_unlock();
	}

	unsigned long index = info->args_ptr;

	if (index < info->types->arg_count)
	{
		if (type == info->types->arg_array[index])
		{
			// type match
			info->args_ptr++;
			return;
		}
		else
		{
			(fprintf)(vasan_global->fp, "--------------------------\n");
			(fprintf)(vasan_global->fp, "Error: Type Mismatch \n");
			(fprintf)(vasan_global->fp, "Index is %lu \n", index);
			(fprintf)(vasan_global->fp, "Callee Type : %lu\n", type);
			(fprintf)(vasan_global->fp, "Caller Type : %lu\n", info->types->arg_array[index]);
			fflush(vasan_global->fp);
			__vasan_backtrace();

			if (!vasan_global->logging_only)
				exit(-1);
		}
	}
	else
	{
		(fprintf)(vasan_global->fp, "--------------------------\n");
		(fprintf)(vasan_global->fp, "Error: Index greater than Argument Count \n");
        (fprintf)(vasan_global->fp, "Index is %d \n", index);
		fflush(vasan_global->fp);
		__vasan_backtrace();

		if (!vasan_global->logging_only)
			exit(-1);
	}

	info->args_ptr++;
}

// Callee Side: Function to reset the counter
void
__vasan_assign_id(int i)
{
	if (!vasan_initialized)
		__vasan_init();

	if (vasan_global->vfunc_cnt)
	{
		__vasan_lock();
		int* cnt;
		if (__vasan_hashmap_get(vasan_global->vfunc_cnt, i, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			__vasan_hashmap_put(vasan_global->vfunc_cnt, i, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
		__vasan_unlock();
	}
}
