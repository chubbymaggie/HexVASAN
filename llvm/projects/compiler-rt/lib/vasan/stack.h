struct stack_elem_t
{
	void* data;
	struct stack_elem_t* next;
};

struct stack_t
{
	struct stack_elem_t* top;
};

unsigned char __vasan_stack_empty(struct stack_t* stack);
void __vasan_stack_push(struct stack_t* stack, void* data);
void* __vasan_stack_pop(struct stack_t* stack);
void* __vasan_stack_top(struct stack_t* stack);
struct stack_t* __vasan_stack_new();
void __vasan_stack_free(struct stack_t* stack);
