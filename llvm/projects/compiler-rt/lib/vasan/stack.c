#include <stdlib.h>
#include "stack.h"

unsigned char stack_empty(struct stack_t* stack)
{
	return (stack && stack->top) ? 1 : 0;
}

void stack_push(struct stack_t* stack, void* data)
{
	if (stack)
	{
		struct stack_elem_t* elem = (struct stack_elem_t*)malloc(sizeof(struct stack_elem_t));
		elem->data = data;
		elem->next = stack->top;
		stack->top = elem;
	}
}

void* stack_pop(struct stack_t* stack)
{
	if (stack && stack->top)
	{
		struct stack_elem_t* elem = stack->top;
		void* result = elem->data;
		stack->top = elem->next;
		free(elem);		
		return result;	    
	}

	return (void*)0;
}

void* stack_top(struct stack_t* stack)
{
	if (stack && stack->top)
		return stack->top->data;
	return (void*)0;		
}

struct stack_t* stack_new()
{
	struct stack_t* result = (struct stack_t*)malloc(sizeof(struct stack_t));
	result->top = (void*)0;
	return result;
}

void stack_free(struct stack_t* stack)
{
	if (stack)
	{
		while(stack->top)
			(void)stack_pop(stack);
		free(stack);
		stack = (struct stack_t*)0;
	}
}
