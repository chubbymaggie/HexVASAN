struct stack_elem_t
{
	void* data;
	struct stack_elem_t* next;
};

struct stack_t
{
	struct stack_elem_t* top;
};

unsigned char stack_empty(struct stack_t* stack);
void stack_push(struct stack_t* stack, void* data);
void* stack_pop(struct stack_t* stack);
void* stack_top(struct stack_t* stack);
struct stack_t* stack_new();
void stack_free(struct stack_t* stack);
