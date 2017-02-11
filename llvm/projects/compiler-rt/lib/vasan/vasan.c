#ifndef NO_BACKTRACE
#include <execinfo.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vasan.h"
#include "stack.h"
#include "hashmap.h"

#define DEBUG
#define MAXPATH 1000

struct vasan_global_state
{

//
// This is where we keep all of the type info that has not been associated with
// a va_list yet. It has to be thread local!
//
#ifdef VASAN_THREAD_SUPPORT
__thread
#endif
struct stack_t* vasan_stack;

//
// This is where we keep all of the type info that HAS been associated with a
// va_list. 
//
#ifdef VASAN_THREAD_SUPPORT
__thread
#endif
map_t vasan_map;

//
// A simple spinlock should suffice to protect accesses to these global maps.
// We're not expecting a lot of contention here.
//
volatile int spinlock;
map_t callsite_cnt;
map_t vfunc_cnt;
unsigned char logging_only;
FILE* fp;
};

__attribute__((visibility("default"))) struct vasan_global_state _vasan_global =
{ (struct stack_t*)0, (map_t)0, 0, (map_t)0, (map_t)0, 0, (FILE*)0 };

static struct vasan_global_state* vasan_global = 0;
static unsigned char vasan_initialized = 0;

// This is musl's debug struct.
struct debug {
    int ver;
    void *head;
    void (*bp)(void);
    int state;
    void *base;
};

// If this points to something, we know we're linked into libc and we should wait
// until TLS is initialized until we can use VASan
struct debug* _dl_debug_addr __attribute__((weak));

// This overrides a weak function in libc that gets called when TLS init is complete
extern void _dl_debug_state(void)
{
	_dl_debug_addr = (void*)0;
}

static unsigned char __vasan_is_tls_active()
{
#if VASAN_THREAD_SUPPORT	
	// If _dl_debug_addr is not set, we're either not linked into libc
	// or we ARE linked into libc but TLS is active
    if (!_dl_debug_addr)
        return 1;
    return 0;
#else
	return 1;
#endif
}

static void __vasan_lock()
{
#if VASAN_THREAD_SUPPORT	
	while(!__sync_bool_compare_and_swap(&vasan_global->spinlock, 0, 1))
		asm volatile("rep; nop" ::: "memory");
#endif
}

static void __vasan_unlock()
{
#if VASAN_THREAD_SUPPORT	
	asm volatile("" ::: "memory");
	vasan_global->spinlock = 0;
#endif
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

// We have to refuse to initialize until TLS is active
static unsigned char __vasan_init()
{
	char path[MAXPATH];
	
    if (!__vasan_is_tls_active())
        return 0;

	// make sure everyone uses the same global state
	vasan_global = dlsym(RTLD_DEFAULT, "_vasan_global");
	vasan_initialized = 1;
	
	if (vasan_global->vasan_stack)
		return 1;

	vasan_global->vasan_stack = __vasan_stack_new();
	vasan_global->vasan_map = __vasan_hashmap_new();

	if (getenv("VASAN_ERR_LOG_PATH") != 0)
	{
		char *home = getenv("VASAN_ERR_LOG_PATH");
		strcpy(path, home);
		strcat(path, "error.txt");
		vasan_global->logging_only = 1;

		// Only track statistics if we're in logging mode
		vasan_global->callsite_cnt = __vasan_hashmap_new();
		vasan_global->vfunc_cnt = __vasan_hashmap_new();

		// open log file and remember the fp
		vasan_global->fp = fopen(path, "a+");
	}

	if (!vasan_global->fp)
		vasan_global->fp = stderr;

    return 1;
}

void __attribute__((destructor)) __vasan_fini()
{
	/*
	stack_free(vasan_global->vasan_stack);
	if (vasan_global->callsite_cnt)
	hashmap_free(vasan_global->callsite_cnt);
	if (vasan_global->vfunc_cnt)
	hashmap_free(vasan_global->vfunc_cnt);
	*/

	if (vasan_global->fp && vasan_global->fp != stderr)
	{
		fclose(vasan_global->fp);
		vasan_global->fp = (FILE*)0;
	}
}

// CallerSide: Function to push the pointer in the stack
void
__vasan_info_push(struct vasan_type_info_tmp *x)
{
    if (!vasan_initialized && !__vasan_init())
        return;

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

    __vasan_stack_push(vasan_global->vasan_stack, x);
}

// We've seen a va_start call.
// Associate the corresponding vasan_type_info_tmp struct with this list
// and store it in the vasan map
void
__vasan_vastart(va_list* list)
{
	if ((!vasan_initialized && !__vasan_init()) || !__vasan_stack_top(vasan_global->vasan_stack))
		return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
	{
		info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_global->vasan_map, (unsigned long)list, (any_t*)info);
	}
	
	info->list_ptr = list;
	info->args_ptr = 0;
	info->types = __vasan_stack_top(vasan_global->vasan_stack);

	// this unassociated type info is now consumed
	__vasan_stack_pop(vasan_global->vasan_stack);
}

// This list is no longer going to be used.
// Remove it from the vasan map
void
__vasan_vaend(va_list* list)
{
	if (!vasan_initialized && !__vasan_init())
		return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) != MAP_MISSING)
	{
		__vasan_hashmap_remove(vasan_global->vasan_map, (unsigned long)list);
		free(info);
	}
}

// Create a copy of another list IN ITS CURRENT STATE!
void
__vasan_vacopy(va_list* src, va_list* dst)
{
	if (!vasan_initialized && !__vasan_init())
		return;

	struct vasan_type_info_full* src_info, *dst_info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)src, (any_t*)&src_info) == MAP_MISSING)
		return;
	
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)dst, (any_t*)&dst_info) == MAP_MISSING)
	{
		dst_info = (struct vasan_type_info_full*)malloc(sizeof(struct vasan_type_info_full));
		__vasan_hashmap_put(vasan_global->vasan_map, (unsigned long)dst, (any_t*)dst_info);
	}

	dst_info->list_ptr = dst;
	dst_info->args_ptr = src_info->args_ptr;
	dst_info->types = src_info->types;
}

// CallerSide: Function to pop the pointer from the stack
void
__vasan_info_pop(int i)
{
    if (!vasan_initialized && !__vasan_init())
        return;

    __vasan_stack_pop(vasan_global->vasan_stack);
}


// New version of the check_index function. You no longer have to figure out
// the index for this one. You just need a list pointer...
void
__vasan_check_index_new(va_list* list, unsigned long type)
{
    if (!vasan_initialized && !__vasan_init())
        return;

	struct vasan_type_info_full* info;
	if (__vasan_hashmap_get(vasan_global->vasan_map, (unsigned long)list, (any_t*)&info) == MAP_MISSING)
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
			(fprintf)(vasan_global->fp, "--------------------------\n");
			(fprintf)(vasan_global->fp, "Error: Type Mismatch \n");
			(fprintf)(vasan_global->fp, "Index is %d \n", index);
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
    if (!vasan_initialized && !__vasan_init())
        return;

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
