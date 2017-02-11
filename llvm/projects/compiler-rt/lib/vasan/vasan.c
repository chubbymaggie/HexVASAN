#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vasan.h"
#include "stack.h"
#include "hashmap.h"

#define DEBUG
#define MAXPATH 1000

//
// This is where we keep all of the type info. It has to be thread local!
//
static __thread struct stack_t* vasan_stack;

//
// A simple spinlock should suffice to protect accesses to these global maps.
// We're not expecting a lot of contention here.
//
static volatile int spinlock = 0;
static map_t callsite_cnt = (map_t*)0;
static map_t vfunc_cnt = (map_t*)0;
static unsigned char vasan_initialized = 0;
static unsigned char logging_only = 0;
static char path[MAXPATH];
static FILE* fp = (FILE*)0;

// This is musl's debug struct.
struct debug {
    int ver;
    void *head;
    void (*bp)(void);
    int state;
    void *base;
};

// We define this as a weak symbol. This symbol is also defined in musl's dynamic linker.
// If we link vasan into musl dynamic linker, their definition of the symbol will take
// precedence over ours and we'll see a non-null value. This way, we can disable
// vasan in the dynamic linker
struct debug* _dl_debug_addr __attribute__((weak));

static unsigned char __vasan_is_tls_active()
{
    // We can use TLS if we're not in the dynamic linker ;)
    if (!_dl_debug_addr)
        return 1;
    return 0;
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
    if (!__vasan_is_tls_active())
        return 0;

	vasan_stack = __vasan_stack_new();
	vasan_initialized = 1;

	if (getenv("VASAN_ERR_LOG_PATH") != 0)
	{
		char *home = getenv("VASAN_ERR_LOG_PATH");
		strcpy(path, home);
		strcat(path, "error.txt");
		logging_only = 1;

		// Only track statistics if we're in logging mode
		callsite_cnt = __vasan_hashmap_new();
		vfunc_cnt = __vasan_hashmap_new();

		// open log file and remember the fp
		fp = fopen(path, "a+");
	}

	if (!fp)
		fp = stderr;

    return 1;
}

void __attribute__((destructor)) __vasan_fini()
{
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
__vasan_info_push(struct callerside_info *x)
{
    if (!vasan_initialized && !__vasan_init())
        return;

	if (callsite_cnt)
	{
		__vasan_lock();
		int* cnt;
		if (__vasan_hashmap_get(callsite_cnt, x->id, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			__vasan_hashmap_put(callsite_cnt, x->id, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
		__vasan_unlock();
	}

    __vasan_stack_push(vasan_stack, x);
}

// CallerSide: Function to pop the pointer from the stack
void
__vasan_info_pop(int i)
{
    if (!vasan_initialized && !__vasan_init())
        return;

    __vasan_stack_pop(vasan_stack);
}

// Callee Side: Function to match the type of the argument with the array
// from top of the stack
void
__vasan_check_index(const char* name, unsigned int* index_ptr, unsigned long type)
{
    if (!vasan_initialized && !__vasan_init())
        return;

	unsigned int index = *index_ptr - 1;
	struct callerside_info* top_frame = (struct callerside_info*)__vasan_stack_top(vasan_stack);

	if (!top_frame)
		return;

	if (index < top_frame->arg_count)
	{
		if (type == top_frame->arg_array[index])
		{
			// type match
			return;
		}
		else
		{
			(fprintf)(fp, "--------------------------\n");
			(fprintf)(fp, "Error: Type Mismatch \n");
			(fprintf)(fp, "FuncName::FileName : %s\n", name);
			(fprintf)(fp, "Index is %d \n", index);
			(fprintf)(fp, "Callee Type : %lu\n", type);
			(fprintf)(fp, "Caller Type : %lu\n", top_frame->arg_array[index]);

			if (!logging_only)
				exit(-1);
		}
	}
	else
	{
		(fprintf)(fp, "--------------------------\n");
		(fprintf)(fp, "Error: Index greater than Argument Count \n");
		(fprintf)(fp, "FuncName::FileName : %s\n", name);
        (fprintf)(fp, "Index is %d \n", index);

		if (!logging_only)
			exit(-1);
	}
}

// Callee Side: Function to reset the counter
void
__vasan_assign_id(int i)
{
    if (!vasan_initialized && !__vasan_init())
        return;

    if (vfunc_cnt)
	{
		__vasan_lock();
		int* cnt;
		if (__vasan_hashmap_get(vfunc_cnt, i, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			__vasan_hashmap_put(vfunc_cnt, i, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
		__vasan_unlock();
	}
}
