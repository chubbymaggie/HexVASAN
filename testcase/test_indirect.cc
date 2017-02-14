#include <stdio.h>
#include <stdarg.h>

int add(int n, ...)
{
    va_list list;
    int total;
    total = 0;

    va_start(list, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, int);
		
	}
    va_end(list);

    return total;
}
int add2(int n, ...)
{
    va_list list;
    int total;
    total = 0;

    va_start(list, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, int);
		
	}
    va_end(list);

    return total;
}

int fn(int (*fnptr)(int, ...))
{

		int value1, value2, value3, value4;
   	int result, result2;

    value1 = 3;
    value2 = 4;
    value3 = 5;
    value4 = 6;


	result = fnptr(5, value1, value1, value2, value3, value4);
	return result;

}

int main(int argc, const char * argv[])
{

  int rs;
	rs = fn(&add);  
    
	return 0;
}


