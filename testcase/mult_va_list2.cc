#include <stdio.h>
#include <stdarg.h>

int add(int n, ...)
{
    va_list list, list2;
    int total;
    total = 0;

    va_start(list, n);
    va_start(list2, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, int) + va_arg(list2, int) ;
		
	}

    va_end(list);
    va_end(list2);

    return total;
}

int main(int argc, const char * argv[])
{

    int value1, value2, value3, value4, result;
    
   result = add(10, value1, value1, value1, value1, value1, value1, value1, value1, value1, value1);
	
    
	return 0;
}


