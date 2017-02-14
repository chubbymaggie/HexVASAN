#include <stdio.h>
#include <stdarg.h>

double add(int n, ...)
{
    va_list list;
    double total;
    total = 0;

    va_start(list, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, double);
		
	}


    va_end(list);

    return total;
}

int main(int argc, const char * argv[])
{

    double value1, value2, value3, value4;
    double result;

    value1 = 3.0;
    value2 = 4.5;
    value3 = 5;
    value4 = 6;

    result = add(9, value1, value2, value3, value1, value2, value3, value1, value2, value3);

   // printf("The sum of %d, %d, %d and %d is %d\n", value1, value2, value3, value4, result);
	
    
	return 0;
}


