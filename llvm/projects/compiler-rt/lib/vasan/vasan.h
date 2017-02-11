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

