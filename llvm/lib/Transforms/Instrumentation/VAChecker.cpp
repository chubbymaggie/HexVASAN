#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"


#include <algorithm>
#include <iostream>
#include <string>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <map>

using namespace llvm;
using std::string;

namespace {

	struct VAChecker : public ModulePass {

		static char ID;
		VAChecker() : ModulePass(ID) {}


		bool doInitialization(Module &M) {

			return true;
		}

		bool doFinalization(Module &M) {

			return false;
		}

		virtual bool runOnModule(Module &M) {
      
			for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
				std::ofstream f_callsite;
		    /*if(F->isVarArg()){
		      	errs() << "VAChecker Function: " << F->getName() << " ";
		      //F.getFunctionType()->dump();
		    }*/

		/*	 for (Function::iterator BB = F->begin(), ET = F->end(); BB != ET; ++BB) {
					BasicBlock& b = *BB;
					for (BasicBlock::iterator I = b.begin(), E = b.end(); I != E; ++I) {
		    
		      if(CallInst *call_inst = dyn_cast<CallInst>(&*I)){
		        if(call_inst->getFunctionType()->isVarArg()){
		          Function *callee = call_inst->getCalledFunction();
		          //direct?
		          if(callee){
		//==============================================
                     if (MDNode *md = call_inst->getMetadata("dbg")) {
                      if (DILocation *dl = dyn_cast<DILocation>(md)) {
                        auto line_no = dl->getLine();
                        // auto col_no = dl->getCol();
                        std::string file_name = dl->getFilename(); 


                //==============================================
								std::string str;
                llvm::raw_string_ostream rso(str);
								call_inst->getFunctionType()->print(rso);
								f_callsite.open("/home/priyam/up_llvm/data/spec/csite.csv", std::ios_base::app | std::ios_base::out);
							  f_callsite << "Direct" << "\t" << callee->getName().str() << "\t" << call_inst->getNumArgOperands() << "\t" << rso.str() << "\t"<< file_name << "\t" << line_no << "\n";

}
}

		          } //if callee
		          //indirect
		          else{ 
				//====================================
                                if (MDNode *md = call_inst->getMetadata("dbg")) {
                      if (DILocation *dl = dyn_cast<DILocation>(md)) {
                        auto line_no = dl->getLine();
                        // auto col_no = dl->getCol();
                        std::string file_name = dl->getFilename(); 
				//=================================
								std::string str;
                llvm::raw_string_ostream rso(str);
								call_inst->getFunctionType()->print(rso);
								f_callsite.open("/home/priyam/up_llvm/data/spec/vfun.csv", std::ios_base::app | std::ios_base::out);
							  f_callsite << "Indirect" << "\t" << "No name" << "\t" << call_inst->getNumArgOperands() << "\t" << rso.str() << "\t" << file_name << "\t" << line_no <<"\n";

}
}

		            
		          }//else
		        }
		      }
		    }
			 }*/
// For collecting data on variadic function ..................
      std::ofstream func_va;
      FunctionType *FT = F->getFunctionType();
      if (F->isVarArg()) {
  
        std::string str;
        llvm::raw_string_ostream rso(str);
        F->getFunctionType()->print(rso);
        uint32_t line_no;
        std::string file_name;
        if (MDNode *md = F->getMetadata("dbg")) {
          if (DISubprogram *dl = dyn_cast<DISubprogram>(md)) {
            line_no = dl->getLine();
            file_name = dl->getFilename();
         // }
       // }
        func_va.open("/home/priyam/up_llvm/data/spec/vfun.csv", std::ios_base::app | std::ios_base::out);
         func_va << F->getName().str() << "\t" << rso.str()
                << "\t"  << file_name << "\t"
                << line_no << "\n";
         }
         }
         func_va.close();
}

//..........................................................			


	f_callsite.close();
			} 
      return false;
    }

		virtual bool runOnFunction(Function &F) {
			
			return false;
		}
	};
}

//register pass
char VAChecker::ID = 0;

INITIALIZE_PASS(VAChecker, "VAChecker",
                "VAChecker",
                false, false);

ModulePass *llvm::createVACheckerPass() {
  return new VAChecker();
}

