#include "vasan.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_report_decorator.h"

#include <csignal>
#include <cxxabi.h>
#include <fstream>
#include <iostream>
#include <list>
#include <signal.h>
#include <stack>
#include <stdio.h>
#include <string.h>
#include <string>
#include <ucontext.h>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <mutex>

#define DEBUG
uint64_t id;

extern thread_local std::stack<callerside_info *> mystack;
extern std::map<int, int> callsite_cnt;
extern std::map<int, int>vfunc_cnt;
std::mutex callee_mutex;

using namespace std;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // Callee Side: Function to match the type of the argument with the array
    // from top of the stack
void check_index(char name[], uint64_t index, uint64_t type) {
 
	std::lock_guard<std::mutex> guard(callee_mutex);
  uint64_t index_8 = (index / 8);
  uint64_t temp_type = 0;
  //printf("Hey I am here\n");  
  if (index_8 < (mystack.top()->arg_count)) {
 
    if (type == (mystack.top()->arg_array[index_8])) {
     
			
    } else {
			
      temp_type = mystack.top()->arg_array[index_8];
      FILE *fp;
      fp = fopen("/home/priyam/up_llvm/yet_mozilla/err_run_type.txt", "a+");
			fprintf(fp, "-------------------------------------------------\n");
      fprintf(fp, "Error: index does not match for index no %d \n", index_8);
      fprintf(fp, "callee side %d\n", type);
      fprintf(fp, "caller side %d\n", temp_type);
      fclose(fp);
      
    }
  }
	else {
		FILE *fp;
    fp = fopen("/home/priyam/up_llvm/yet_mozilla/err_run_index.txt",
               "a+");	
		fprintf(fp, "-------------------------------------------------\n");
    fprintf(fp, "Error: Index is bigger than argument count \n");
    fclose(fp);
		

	}
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // Callee Side: Function to reset the counter
    void
    assign_id(int i) {
		std::lock_guard<std::mutex> guard(callee_mutex);
    vfunc_cnt[i]++;

}
