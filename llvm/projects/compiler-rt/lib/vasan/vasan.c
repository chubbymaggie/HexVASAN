#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#ifndef NO_BACKTRACE
#include <execinfo.h>
#endif
//#include "vasan.h"
//#include "stack.h"
//#include "hashmap.h"

#define DEBUG
#define MAXPATH 1000
#define INITIAL_SIZE 1024
#define MAP_MISSING -3  /* No such element */
#define MAP_FULL -2 /* Hashmap is full */
#define MAP_OMEM -1 /* Out of Memory */
#define MAP_OK 0 /* OK */

/*
 * any_t is a pointer.  This allows you to put arbitrary structures in
 * the hashmap.
 */
typedef void *any_t;

/*
 * PFany is a pointer to a function that can take two any_t arguments
 * and return an integer. Returns status code..
 */
typedef int (*PFany)(any_t, any_t);

/*
 * map_t is a pointer to an internally maintained data structure.
 * Clients of this package do not need to know how hashmaps are
 * represented.  They see and manipulate only map_t's. 
 */
typedef any_t map_t;


// this is what the instrumentation code will pass to the runtime.
// it's type info that has not been associated with a va_list yet.
struct vasan_type_info_tmp
{
	unsigned long id;
	unsigned long arg_count;
	unsigned long*  arg_array;	
};

struct vasan_type_info_full
{
	va_list* list_ptr;
	unsigned int args_ptr;
	struct vasan_type_info_tmp* types;
};

// We need to keep keys and values
typedef struct _hashmap_element{
	int key;
	int in_use;
	any_t data;
} hashmap_element;

// A hashmap has some maximum size and current size,
// as well as the data to hold.
typedef struct _hashmap_map{
	int table_size;
	int size;
	hashmap_element *data;
} hashmap_map;

struct stack_elem_t
{
	void* data;
	struct stack_elem_t* next;
};

struct stack_t
{
	struct stack_elem_t* top;
};


static inline unsigned char __vasan_stack_empty(struct stack_t* stack);
static inline void __vasan_stack_push(struct stack_t* stack, void* data);
static inline void* __vasan_stack_pop(struct stack_t* stack);
static inline void* __vasan_stack_top(struct stack_t* stack);
static inline struct stack_t* __vasan_stack_new();
static inline void __vasan_stack_free(struct stack_t* stack);

/*
 * Return an empty hashmap. Returns NULL if empty.
 */
static inline map_t __vasan_hashmap_new();

/*  
 * Iteratively call f with argument (item, data) for 
 * each element data in the hashmap. The function must
 * return a map status code. If it returns anything other
 * than MAP_OK the traversal is terminated. f must
 * not reenter any hashmap functions, or deadlock may arise.
 */
static inline int __vasan_hashmap_iterate(map_t in, PFany f, any_t item);

/*
 * Add an element to the hashmap. Return MAP_OK or MAP_OMEM.
 */
static inline int __vasan_hashmap_put(map_t in, int key, any_t value);

/*
 * Get an element from the hashmap. Return MAP_OK or MAP_MISSING.
 */
static inline int __vasan_hashmap_get(map_t in, int key, any_t *arg);

/*
 * Remove an element from the hashmap. Return MAP_OK or MAP_MISSING.
 */
static inline int __vasan_hashmap_remove(map_t in, int key);

/*
 * Get any element. Return MAP_OK or MAP_MISSING.
 * remove - should the element be removed from the hashmap
 */
static inline int __vasan_hashmap_get_one(map_t in, any_t *arg, int remove);

/*
 * Free the hashmap
 */
static inline void __vasan_hashmap_free(map_t in);

/*
 * Get the current size of a hashmap
 */
static inline int __vasan_hashmap_length(map_t in);


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
		__asm__ __volatile__("rep; nop" ::: "memory");
}

static void __vasan_unlock()
{
	__asm__ __volatile__("" ::: "memory");
	spinlock = 0;
}

static inline unsigned char __vasan_stack_empty(struct stack_t* stack)
{
	return (stack && stack->top) ? 1 : 0;
}

static inline void __vasan_stack_push(struct stack_t* stack, void* data)
{
	if (stack)
	{
		struct stack_elem_t* elem = (struct stack_elem_t*)malloc(sizeof(struct stack_elem_t));
		elem->data = data;
		elem->next = stack->top;
		stack->top = elem;
	}
}

static inline void* __vasan_stack_pop(struct stack_t* stack)
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

static inline void* __vasan_stack_top(struct stack_t* stack)
{
	if (stack && stack->top)
		return stack->top->data;
	return (void*)0;		
}

static inline struct stack_t* __vasan_stack_new()
{
	struct stack_t* result = (struct stack_t*)malloc(sizeof(struct stack_t));
	result->top = (void*)0;
	return result;
}

static inline void __vasan_stack_free(struct stack_t* stack)
{
	if (stack)
	{
		while(stack->top)
			(void)__vasan_stack_pop(stack);
		free(stack);
		stack = (struct stack_t*)0;
	}
}

/*
 * Return an empty hashmap, or NULL on failure.
 */
static inline map_t __vasan_hashmap_new() {
	hashmap_map* m = (hashmap_map*) malloc(sizeof(hashmap_map));
	if(!m) goto err;

	m->data = (hashmap_element*) calloc(INITIAL_SIZE, sizeof(hashmap_element));
	if(!m->data) goto err;

	m->table_size = INITIAL_SIZE;
	m->size = 0;

	return m;
err:
	if (m)
		__vasan_hashmap_free(m);
	return NULL;
}

/*
 * Hashing function for an integer
 */
static inline unsigned int __vasan_hashmap_hash_int(hashmap_map * m, unsigned int key){
	/* Robert Jenkins' 32 bit Mix Function */
	key += (key << 12);
	key ^= (key >> 22);
	key += (key << 4);
	key ^= (key >> 9);
	key += (key << 10);
	key ^= (key >> 2);
	key += (key << 7);
	key ^= (key >> 12);

	/* Knuth's Multiplicative Method */
	key = (key >> 3) * 2654435761;

	return key % m->table_size;
}

/*
 * Return the integer of the location in data
 * to store the point to the item, or MAP_FULL.
 */
static inline int __vasan_hashmap_hash(map_t in, int key){
	int curr;
	int i;

	/* Cast the hashmap */
	hashmap_map* m = (hashmap_map *) in;

	/* If full, return immediately */
	if(m->size == m->table_size) return MAP_FULL;

	/* Find the best index */
	curr = __vasan_hashmap_hash_int(m, key);

	/* Linear probling */
	for(i = 0; i< m->table_size; i++){
		if(m->data[curr].in_use == 0)
			return curr;

		if(m->data[curr].key == key && m->data[curr].in_use == 1)
			return curr;

		curr = (curr + 1) % m->table_size;
	}

	return MAP_FULL;
}

/*
 * Doubles the size of the hashmap, and rehashes all the elements
 */
static inline int __vasan_hashmap_rehash(map_t in){
	int i;
	int old_size;
	hashmap_element* curr;

	/* Setup the new elements */
	hashmap_map *m = (hashmap_map *) in;
	hashmap_element* temp = (hashmap_element *)
		calloc(2 * m->table_size, sizeof(hashmap_element));
	if(!temp) return MAP_OMEM;

	/* Update the array */
	curr = m->data;
	m->data = temp;

	/* Update the size */
	old_size = m->table_size;
	m->table_size = 2 * m->table_size;
	m->size = 0;

	/* Rehash the elements */
	for(i = 0; i < old_size; i++){
		int status = __vasan_hashmap_put(m, curr[i].key, curr[i].data);
		if (status != MAP_OK)
			return status;
	}

	free(curr);

	return MAP_OK;
}

/* 
 * Add a pointer to the hashmap with some key
 */
static inline int __vasan_hashmap_put(map_t in, int key, any_t value){
	int index;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* Find a place to put our value */
	index = __vasan_hashmap_hash(in, key);
	while(index == MAP_FULL){
		if (__vasan_hashmap_rehash(in) == MAP_OMEM) {
			return MAP_OMEM;
		}
		index = __vasan_hashmap_hash(in, key);
	}

	/* Set the data */
	m->data[index].data = value;
	m->data[index].key = key;
	m->data[index].in_use = 1;
	m->size++;

	return MAP_OK;
}

/*
 * Get your pointer out of the hashmap with a key
 */
static inline int __vasan_hashmap_get(map_t in, int key, any_t *arg){
	int curr;
	int i;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* Find data location */
	curr = __vasan_hashmap_hash_int(m, key);

	/* Linear probing, if necessary */
	for(i = 0; i< m->table_size; i++){

		if(m->data[curr].key == key && m->data[curr].in_use == 1){
			*arg = (int *) (m->data[curr].data);
			return MAP_OK;
		}

		curr = (curr + 1) % m->table_size;
	}

	*arg = NULL;

	/* Not found */
	return MAP_MISSING;
}

/*
 * Get a random element from the hashmap
 */
static inline int __vasan_hashmap_get_one(map_t in, any_t *arg, int remove){
	int i;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* On empty hashmap return immediately */
	if (__vasan_hashmap_length(m) <= 0)
		return MAP_MISSING;

	/* Linear probing */
	for(i = 0; i< m->table_size; i++)
		if(m->data[i].in_use != 0){
			*arg = (any_t) (m->data[i].data);
			if (remove) {
				m->data[i].in_use = 0;
				m->size--;
			}
			return MAP_OK;
		}

	return MAP_OK;
}

/*
 * Iterate the function parameter over each element in the hashmap.  The
 * additional any_t argument is passed to the function as its first
 * argument and the hashmap element is the second.
 */
static inline int __vasan_hashmap_iterate(map_t in, PFany f, any_t item) {
	int i;

	/* Cast the hashmap */
	hashmap_map* m = (hashmap_map*) in;

	/* On empty hashmap, return immediately */
	if (__vasan_hashmap_length(m) <= 0)
		return MAP_MISSING;

	/* Linear probing */
	for(i = 0; i< m->table_size; i++)
		if(m->data[i].in_use != 0) {
			any_t data = (any_t) (m->data[i].data);
			int status = f(item, data);
			if (status != MAP_OK) {
				return status;
			}
		}

	return MAP_OK;
}

/*
 * Remove an element with that key from the map
 */
static inline int __vasan_hashmap_remove(map_t in, int key){
	int i;
	int curr;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

    /* Find key */
	curr = __vasan_hashmap_hash_int(m, key);

	/* Linear probing, if necessary */
	for(i = 0; i< m->table_size; i++){
		if(m->data[curr].key == key && m->data[curr].in_use == 1){
			/* Blank out the fields */
			m->data[curr].in_use = 0;
			m->data[curr].data = NULL;
			m->data[curr].key = 0;

			/* Reduce the size */
			m->size--;
			return MAP_OK;
		}
		curr = (curr + 1) % m->table_size;
	}

	/* Data not found */
	return MAP_MISSING;
}

/* Deallocate the hashmap */
static inline void __vasan_hashmap_free(map_t in){
	hashmap_map* m = (hashmap_map*) in;
	free(m->data);
	free(m);
}

/* Return the length of the hashmap */
static inline int __vasan_hashmap_length(map_t in){
	hashmap_map* m = (hashmap_map *) in;
	if(m != NULL) return m->size;
	else return 0;
}


// We have to refuse to initialize until TLS is active
static unsigned char __vasan_init()
{
	char path[MAXPATH];

	// make global state init thread safe
	__vasan_lock();
	
	vasan_initialized = 1;
	vasan_stack = __vasan_stack_new();
	vasan_map   = __vasan_hashmap_new();
	no_recurse  = 0;

	if (fp)
	{
		__vasan_unlock();
		return 0;
	}

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

	char* disabled = getenv("VASAN_NO_ERROR_REPORTING");

	if (disabled && strcmp(disabled, "0"))
		fp = (FILE*)0;

	__vasan_unlock();
    return 1;
}

static void __attribute__((destructor)) __vasan_fini()
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
		else if (fp)
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
	else if (fp)
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
