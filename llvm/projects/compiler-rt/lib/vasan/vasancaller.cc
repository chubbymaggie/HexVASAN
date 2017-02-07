#include "vasan.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_report_decorator.h"

#include <csignal>
#include <cxxabi.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <stack>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#define gettid() syscall(SYS_gettid)

std::stack<callerside_info *> mystack;
std::map<int, int> callsite_cnt;
std::map<int, int> vfunc_cnt;

using namespace std;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // CallerSide: Function to push the pointer in the stack
    void
    info_push(callerside_info *x) {

  callsite_cnt[x->id]++;
  mystack.push(x);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE

    // CallerSide: Function to pop the pointer from the stack
    void
    info_pop(int i) {

  if (!mystack.empty())
    mystack.pop();
}
