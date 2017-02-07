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
#include <map>
#include <mutex>
#include <signal.h>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#define DEBUG
#define MAXPATH 1000

uint64_t id;
extern std::stack<callerside_info *> mystack;
extern std::map<int, int> callsite_cnt;
extern std::map<int, int> vfunc_cnt;

using namespace std;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // Callee Side: Function to match the type of the argument with the array
    // from top of the stack
    void
    check_index(char name[], uint32_t *index, uint64_t type) {

  std::ofstream func_va;
  std::string pathname;
  if (getenv("VASAN_ERR_LOG_PATH") != nullptr) {
    char *home = getenv("VASAN_ERR_LOG_PATH");
    char path[MAXPATH];
    strcpy(path, home);
    //printf("Path is %s\n", home);
    pathname = strcat(path, "error.txt");
  }
  uint32_t index_8 = *index - 1;
  uint64_t temp_type = 0;

  if (index_8 < (mystack.top()->arg_count)) {

    if (type == (mystack.top()->arg_array[index_8])) {

    } else {
     
      func_va.open(pathname, std::ios_base::app | std::ios_base::out);
      temp_type = mystack.top()->arg_array[index_8];
      func_va << "--------------------------\n";
      func_va << "Error: Type Mismatch \n";
      func_va << "FuncName::FileName : " << name << "\n";
      func_va << "Index is " << index_8 << "\n";
      func_va << "Callee Type : " << type << "\n";
      func_va << "Caller Type : " << temp_type << "\n";

      func_va.close();

     
    }
  } else {
    func_va.open(pathname, std::ios_base::app | std::ios_base::out);
		func_va << "--------------------------\n";
    func_va << "Error: Index greater than Argument Count \n";
		func_va << "FuncName::FileName : " << name << "\n";
    func_va.close();
    
  }
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // Callee Side: Function to reset the counter
    void
    assign_id(int i) {

  vfunc_cnt[i]++;
}
