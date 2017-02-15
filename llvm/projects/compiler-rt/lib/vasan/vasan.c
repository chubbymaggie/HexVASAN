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

// only holds the call site info
static __thread struct stack_t* vasan_stack;
static __thread unsigned char no_recurse;

// maps va_lists onto their call site info
static __thread map_t vasan_map;

// protects accesses to global structures
// it's just a simple spinlock as we don't
// expect much contention
static volatile int spinlock = 0;

// if set to 1, we log violations but don't terminate
// the program when a violation is triggered
static unsigned char logging_only = 0;

// file we're logging to. can be either stderr or a real file
static FILE* fp = (FILE*)0;

// When set to 1, it's safe to access the global state
static __thread unsigned char vasan_initialized = 0;


static void __vasan_backtrace()
{
#ifndef NO_BACKTRACE
    void *trace[16];
	char **messages = (char **)NULL;
	int i, trace_size = 0;

	trace_size = backtrace(trace, 16);
	messages = backtrace_symbols(trace, trace_size);
	(fprintf)(fp, "Backtrace:\n");
	for (i=0; i<trace_size; ++i)
		(fprintf)(fp, "[%d] %s\n", i, messages[i]);
	free(messages);
#endif
}

static void __vasan_lock()
{
	while(!__sync_bool_compare_and_swap(&spinlock, 0, 1))
		asm volatile("rep; nop" ::: "memory");
}

static void __vasan_unlock()
{
	asm volatile("" ::: "memory");
	spinlock = 0;
}

// We have to refuse to initialize until TLS is active
static unsigned char __vasan_init()
{
	char path[MAXPATH];

	// make global state init thread safe
	__vasan_lock();
	
	if (vasan_stack)
	{
		__vasan_unlock();
		return 1;
	}

	vasan_initialized = 1;
	vasan_stack = __vasan_stack_new();
	vasan_map   = __vasan_hashmap_new();
	no_recurse  = 0;

	char *home = getenv("VASAN_ERR_LOG_PATH");

	if (home != 0)
	{
		strcpy(path, home);
		strcat(path, "error.txt");

		// open log file and remember the fp. fopen is a vararg func. We must
		// ensure that it doesn't recursively call __vasan_init
		no_recurse = 1;
		fp = fopen(path, "a+");
		no_recurse = 0;

		// remember the fp
		logging_only = 1;
	}

	if (!fp)
		fp = stderr;

	__vasan_unlock();
    return 1;
}

void __attribute__((destructor)) __vasan_fini()
{
	// TODO: Do a proper cleanup here
	
	/*
	stack_free(vasan_stack);
	if (callsite_cnt)
	hashmap_free(callsite_cnt);
	if (vfunc_cnt)
	hashmap_free(vfunc_cnt);
	*/

	if (fp && fp != stderr)
	{
		fclose(fp);
		fp = (FILE*)0;
	}
}

// CallerSide: Function to push the pointer in the stack
void
__vasan_info_push(struct vasan_type_info_tmp *x)
{
	if (!vasan_initialized)
		__vasan_init();

	__vasan_stack_push(vasan_stack, x);
}

// We've seen a va_start call.
// Associate the corresponding vasan_type_info_tmp struct with this list
// and store it in the vasan map
void
__vasan_vastart(va_list* list)
{	
	if (!vasan_initialized)
		__vasan_init();

	if (no_recurse)
		return;

	struct vasan_type_info_tmp* latest = __vasan_stack_top(vasan_stack);

	if (!latest)
		return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
	{
		info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_map, (unsigned long)list, (any_t*)info);
	}
	
	info->list_ptr = list;
	info->args_ptr = 0;
	info->types = latest;
}

// This list is no longer going to be used.
// Remove it from the vasan map
void
__vasan_vaend(va_list* list)
{
	if (!vasan_initialized)
		__vasan_init();

	if (no_recurse)
		return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_map, (unsigned long)list, (any_t*)&info) != MAP_MISSING)
	{
		__vasan_hashmap_remove(vasan_map, (unsigned long)list);
		free(info);
	}
}

// Create a copy of another list IN ITS CURRENT STATE!
void
__vasan_vacopy(va_list* src, va_list* dst)
{
	if (!vasan_initialized)
		__vasan_init();

	if (no_recurse)
		return;

	struct vasan_type_info_full* src_info, *dst_info;
	if (__vasan_hashmap_get(vasan_map, (unsigned long)src, (any_t*)&src_info) == MAP_MISSING)
		return;
	
	if (__vasan_hashmap_get(vasan_map, (unsigned long)dst, (any_t*)&dst_info) == MAP_MISSING)
	{
		dst_info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_map, (unsigned long)dst, (any_t*)dst_info);
	}

	dst_info->list_ptr = dst;
	dst_info->args_ptr = src_info->args_ptr;
	dst_info->types = src_info->types;
}

// CallerSide: Function to pop the pointer from the stack
void
__vasan_info_pop(int i)
{
	if (!vasan_initialized)
		__vasan_init();

	__vasan_stack_pop(vasan_stack);
}


// New version of the check_index function. You no longer have to figure out
// the index for this one. You just need a list pointer...
void
__vasan_check_index_new(va_list* list, unsigned long type)
{
	if (!vasan_initialized)
		__vasan_init();

	if (no_recurse)
		return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
		return;

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
			// Temporarily disable recursion so we can safely call fprintf
			no_recurse = 1;			
			(fprintf)(fp, "--------------------------\n");
			(fprintf)(fp, "Error: Type Mismatch \n");
			(fprintf)(fp, "Index is %lu \n", index);
			(fprintf)(fp, "Callee Type : %lu\n", type);
			(fprintf)(fp, "Caller Type : %lu\n", info->types->arg_array[index]);
			fflush(fp);
			__vasan_backtrace();
			no_recurse = 0;

			if (!logging_only)
				exit(-1);
		}
	}
	else
	{
		no_recurse = 1;
		(fprintf)(fp, "--------------------------\n");
		(fprintf)(fp, "Error: Index greater than Argument Count \n");
        (fprintf)(fp, "Index is %d \n", index);
		fflush(fp);
		__vasan_backtrace();
		no_recurse = 0;

		if (!logging_only)
			exit(-1);
	}

	info->args_ptr++;
}
