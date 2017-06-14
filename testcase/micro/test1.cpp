#include <stdio.h>
#include <stdarg.h>
#include <ctime>
#include <cstdint>
#include <chrono>
#include <iostream>

int add(int n, ...) {

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

int main(int argc, const char * argv[]) {

    int value1, value2, value3, value4, result;
    clock_t begin = clock();
    result = add(3, value1, value2, value3);
    clock_t end = clock();
    double elapsed_secs = double(end - begin);
    std::cout << "Using ctime: Time difference = " << elapsed_secs << std::endl;    

    /* Using chrono */
    std::chrono::steady_clock::time_point begin2 = std::chrono::steady_clock::now();    
    result = add(3, value1, value2, value3);
    std::chrono::steady_clock::time_point end2= std::chrono::steady_clock::now();
    std::cout << "Using Chrono: Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(end2 - begin2).count() << "  microsec" <<std::endl;
    std::cout << "Using Chrono: Time difference = " << std::chrono::duration_cast<std::chrono::nanoseconds> (end2 - begin2).count() << "  nanosec" <<std::endl;

    return 0;
}
