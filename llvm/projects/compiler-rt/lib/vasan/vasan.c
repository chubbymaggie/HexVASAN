#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vasan.h"
#include "stack.h"
#include "hashmap.h"

#define DEBUG
#define MAXPATH 1000

static struct stack_t* vasan_stack = (struct stack_t*)0;
static map_t callsite_cnt = (map_t*)0;
static map_t vfunc_cnt = (map_t*)0;
static unsigned char logging_only = 0;
static char path[MAXPATH];
static FILE* fp = (FILE*)0;

void __attribute__((constructor)) init()
{
	vasan_stack = stack_new();
	callsite_cnt = hashmap_new();
	vfunc_cnt = hashmap_new();

	if (getenv("VASAN_ERR_LOG_PATH") != 0)
	{
		char *home = getenv("VASAN_ERR_LOG_PATH");
		strcpy(path, home);
		strcat(path, "error.txt");
		logging_only = 1;

		// open log file and remember the fp
		fp = fopen(path, "a+");
	}

	if (!fp)
		fp = stderr;
}

void __attribute__((destructor)) fini()
{
	/*
	stack_free(vasan_stack);
	hashmap_free(callsite_cnt);
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
info_push(struct callerside_info *x)
{
	if (callsite_cnt)
	{
		int* cnt;
		if (hashmap_get(callsite_cnt, x->id, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			hashmap_put(callsite_cnt, x->id, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
	}

	if (vasan_stack)
		stack_push(vasan_stack, x);
}

// CallerSide: Function to pop the pointer from the stack
void
info_pop(int i)
{
	stack_pop(vasan_stack);
}

// Callee Side: Function to match the type of the argument with the array
// from top of the stack
void
check_index(const char* name, unsigned int* index_ptr, unsigned long type)
{
	unsigned int index = *index_ptr - 1;
	struct callerside_info* top_frame = (struct callerside_info*)stack_top(vasan_stack);

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
assign_id(int i)
{
	if (vfunc_cnt)
	{
		int* cnt;
		if (hashmap_get(vfunc_cnt, i, (any_t*)&cnt) == MAP_MISSING)
		{
			cnt = (int*)malloc(sizeof(int));
			*cnt = 1;
			hashmap_put(vfunc_cnt, i, (any_t)cnt);
		}
		else
			*cnt = *cnt + 1;
	}
}
