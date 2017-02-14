#include <stdio.h>
#include <stdarg.h>

struct va {
 int a;
 double b; 
};

int add(int n, ...)
{
    va_list list;
    int total;
    total = 0;

    va_start(list, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, va).a;
		
	}


    va_end(list);

    return total;
}

int main(int argc, const char * argv[])
{

		struct va st_a;
		int result;
    va value1, value2, value3, value4;
    
   result = add(10, value1, value1, value1, value1, value1, value1, value1, value1, value1, value1);
  
	
    
	return 0;
}


