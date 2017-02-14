#include <stdio.h>
#include <stdarg.h>

 int add(int n, int d, ...)
{
    va_list list;
    int total;
    total = 0;

    va_start(list, n);

    for (int i=0; i < n; i++) {
        total = total + va_arg(list, int);
		
	}
	total = total + d;

  va_end(list);
	//printf("total is %d \n", total);
    return total;
}

int main(int argc, const char * argv[])
{

    int value1, value2, value3, value4;
    int result, result2;

    value1 = 3;
    value2 = 4;
    value3 = 5;
    value4 = 6;

   
  //result = add(8, value1, value1, value1, value1, value2, value3, value1, value2);
	result = add(4, 5, value1, value2, value3, value4, value1, value1, value1, value1);
	
	
    
	return 0;
}


