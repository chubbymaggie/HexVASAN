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

#define DEBUG
uint64_t id;

extern std::stack<callerside_info *> mystack;
extern std::map<int, int> callsite_cnt;
extern std::map<int, int>vfunc_cnt;

using namespace std;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // Callee Side: Function to match the type of the argument with the array
    // from top of the stack
void check_index(char name[], uint32_t index, uint64_t type) {
 
  uint32_t index_8 = (index / 8);
  uint64_t temp_type = 0;
  printf("Hey I am here\n");
  if(mystack.top()->id == 1) {
    
      FILE *fp;
      fp = fopen("/home/priyam/up_llvm/var/indirect_funcname.txt", "a+");
      fprintf(fp, "-------------------------------------------------\n");
      fprintf(fp, "Function Name %s\n", name);
      fprintf(fp, "argument count %d\n", mystack.top()->arg_count);
      fclose(fp);

  }
	
    
      FILE *fp;
      fp = fopen("/home/priyam/up_llvm/var/indirect_funcname.txt", "a+");
      fprintf(fp, " Simple Print------------------------------------------------\n");
      fprintf(fp, "Function Name %s\n", name);
      fprintf(fp, "argument count %d\n", mystack.top()->arg_count);
      fclose(fp);

 

  
  if (index_8 < (mystack.top()->arg_count)) {
 
    if (type == (mystack.top()->arg_array[index_8])) {
     
			
    } else {
			
      temp_type = mystack.top()->arg_array[index_8];
      FILE *fp;
      fp = fopen("/home/priyam/up_llvm/data_m/err_mozilla_run.txt", "a+");
			fprintf(fp, "-------------------------------------------------\n");
      fprintf(fp, "Error: index does not match for index no %d \n", index_8);
      fprintf(fp, "callee side %d\n", type);
      fprintf(fp, "caller side %d\n", temp_type);
      fclose(fp);
      
    }
  }
	else {
		FILE *fp;
    fp = fopen("/home/priyam/up_llvm/data_m/err_mozilla_run.txt",
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
    vfunc_cnt[i]++;

}

/*extern "C" SANITIZER_INTERFACE_ATTRIBUTE
__attribute__((destructor))
void destroy_vasan() {
  std::ofstream func_va;
	//std::string pid = getpid();
  char pathname[100];
	std::sprintf(pathname, "/home/priyam/up_llvm/data/runtime_vfunc%d.csv", getpid() ); 
	func_va.open(pathname, std::ios_base::app | std::ios_base::out);

	for ( std::map<int, int>::const_iterator it = vfunc_cnt.begin(); it != vfunc_cnt.end(); it++) {
		
		func_va << it->first << "\t" << it->second << "\n";   

	}
  func_va.close();

}*/
