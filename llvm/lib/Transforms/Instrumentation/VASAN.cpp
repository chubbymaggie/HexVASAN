#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TableGen/Error.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>id
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>

// std::map<std::string , long int> variadic_map;
std::map<llvm::Value *, long int> variadic_map;
// std::map<std::string, boost::uuids::uuid> variadic_map; //FIXME right now I
// am putting a string, need to change later
using namespace llvm;
using std::string;
// FIXME: Need to parse cmdline if we want to create csv files in output file
// directory
//=============================================
// static cl::ParseCommandLineOptions
// static cl::opt<bool> pathinf

//==============================================

namespace {

struct VASAN : public ModulePass {

  static char ID;
  LLVMContext *Context;

  VASAN() : ModulePass(ID) {} 

  bool doInitialization(Module &M) { return true; }

  bool doFinalization(Module &M) { return false; }

  bool check_incoming1(PHINode *phi, Value **index_reg, Type *ty, Value *one,
                       Value *eight);
  bool check_incoming2(PHINode *phi, Value **add_five, Type *ty,
                       Value *forty_eight, Value *bit_inst2);

  virtual bool runOnModule(Module &M) {

    int counter = 0;
    Module *N_M;
    N_M = &M;
    LLVMContext &Ctx = M.getContext();
    Context = &M.getContext();

    Type *VoidTy = Type::getVoidTy(Ctx);
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *Int8Ty = Type::getInt8Ty(Ctx);
    Type *Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
    Type *Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
    Type *Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));

    auto dm = M.getDataLayout();
    auto ty = dm.getIntPtrType(Ctx);
    std::mt19937 gen; // FIXME Seed the engine with an unsigned int

    // This part is for check all the variadic functions and storing all the
    // necessary information in a csv file
    // The information we are storing: i) unique id ii) Function name iii)
    // Function Type iv) External Linkage v) File name
    // vi) line no vii) users of the variadic function other than direct call
    // viii) total number of uses
    srand(time(nullptr));

    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
      std::ofstream func_va;
      Value *funcptr;
      FunctionType *FT = F->getFunctionType();

      if (Function *Fnew = dyn_cast<Function>(F)) {
        funcptr = dyn_cast<Value>(Fnew);
      }
      if (F->isVarArg()) {

        long int unique_id = rand();
        variadic_map.insert(
            std::pair<llvm::Value *, long int>(funcptr, unique_id));
        std::string str;
        llvm::raw_string_ostream rso(str);
        F->getFunctionType()->print(rso);
        std::queue<User *> func_user;
        uint32_t line_no;
        std::string file_name;
        if (MDNode *md = F->getMetadata("dbg")) {
          if (DISubprogram *dl = dyn_cast<DISubprogram>(md)) {
            line_no = dl->getLine();
            file_name = dl->getFilename();
          }
        }
        std::string pathname =
            "/home/priyam/up_llvm/data_m/vfunc/" + file_name +
            "_vfunc.tsv";
        func_va.open(
            pathname,
            std::ios_base::app |
                std::ios_base::out); // FIXME the path needs to be fixed

        func_va << unique_id << "\t" << F->getName().str() << "\t" << rso.str()
                << "\t" << F->getLinkage() << "\t" << file_name << "\t"
                << line_no;
        uint32_t user_count = 0;
        for (User *f_user : F->users()) {
          user_count++;
        }

        func_va << "\t" << user_count;
        func_va << "\n";
        // for instrumenting the id in the runtime ...
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          BasicBlock &b = *BB;
          for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

            if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

              Function *ft = call_inst->getCalledFunction();

              if ((ft != nullptr) &&
                  ((ft->getIntrinsicID() == llvm::Intrinsic::vastart))) {

                IRBuilder<> Builder(call_inst);
                Value *Param[] = {ConstantInt::get(Int64Ty, unique_id)};
                Constant *GInit = N_M->getOrInsertFunction("assign_id", VoidTy,
                                                           Int64Ty, nullptr);
                Builder.CreateCall(GInit, Param);
              }
            }
          }
        }
        // for instrumenting the unique id in the runtime...
      }
      func_va.close();
    }
    //================csv information ends here
    //=============================================================
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {

      FunctionType *FT = F->getFunctionType();
      Value *bit_inst2 = 0;
      int va_start_count = 0;
      int flag_va_intrinsic = 0;

      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
        BasicBlock &b = *BB;

        //========================================checking for vastart
        // count===========================
        for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

          if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

            Function *ft = call_inst->getCalledFunction();

            if ((ft != nullptr) &&
                ((ft->getIntrinsicID() == llvm::Intrinsic::vastart) ||
                 (ft->getIntrinsicID() == llvm::Intrinsic::vaend))) {
              if (ft->getIntrinsicID() == llvm::Intrinsic::vastart) {
                va_start_count = va_start_count + 1;
              }
            }
          }
        }
      }
      // errs() << "va_star count is "<< va_start_count << "\n";
      //======================================checking ends for va_start
      // count======================
      if (va_start_count < 2) {
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          BasicBlock &b = *BB;
          for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

            int bit_width = 0;
            Value *index_reg = 0;
            Value *base;
            Value *add_five = 0;

            Value *one = ConstantInt::get(Type::getInt32Ty(Ctx), 1);
            Value *five = ConstantInt::get(Type::getInt32Ty(Ctx), 5);
            Value *eight = ConstantInt::get(Type::getInt32Ty(Ctx), 8);
            Value *forty_eight = ConstantInt::get(Type::getInt32Ty(Ctx), 48);

            Value *Idxs[] = {ConstantInt::get(Int32Ty, 0),
                             ConstantInt::get(Int32Ty, 2)};
            if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

              Function *ft = call_inst->getCalledFunction();

              if ((ft != nullptr) &&
                  ((ft->getIntrinsicID() == llvm::Intrinsic::vastart) ||
                   (ft->getIntrinsicID() == llvm::Intrinsic::vaend))) {

                if (ft->getIntrinsicID() == llvm::Intrinsic::vastart) {

                  /*IRBuilder<> Builder(call_inst);
                  Value *Param[] = {ConstantInt::get(Int64Ty, counter)};
                  Constant *GInit = N_M->getOrInsertFunction(
                      "assign_id", VoidTy, Int64Ty, nullptr);
                  Builder.CreateCall(GInit, Param);*/

                  /* after */
                  // here checking for the operand that means [arraydecay1] is a
                  // bit inst or not, after that I have to look for the user of
                  // this and look for the list of array and then the user of
                  // the
                  // list<FIXME>
                  if (BitCastInst *binst =
                          dyn_cast<BitCastInst>(call_inst->getArgOperand(0))) {

                    IRBuilder<> builder(call_inst->getNextNode());
                    auto gep_b = builder.CreateGEP(binst->getOperand(0), Idxs);
                    base = builder.CreateLoad(gep_b);
                    bit_inst2 = builder.CreatePtrToInt(base, ty);
                    assert(bit_inst2);

                    // here the 1st operand of the gep instruction is tha
                    // va_list
                    // I am looking for
                    if (GetElementPtrInst *gepinst =
                            dyn_cast<GetElementPtrInst>(
                                (binst->getOperand(0)))) {
                      std::queue<User *> queue_user;
                      for (User *ur : gepinst->getOperand(0)->users())
                        //  User *ur= gepinst->getOperand(0)->users();
                        queue_user.push(ur);
                      int flag_while_brk = 0;
               
                      while ((!queue_user.empty())) {

                        User *urv = queue_user.front();
                        queue_user.pop();

                        if (BranchInst *b = dyn_cast<BranchInst>(urv)) {
                          flag_while_brk = 1;
	                  
                          if (b->getSuccessor(0) != nullptr) {
                            BasicBlock *Bck1 =   b->getSuccessor(0) ;
                            if (b->getSuccessor(1) != nullptr) {
                              BasicBlock *Bck2 = b->getSuccessor(1);
                               

                              int loop1 = 0;
                              int loop2 = 0;
															const BasicBlock *Bck1_suc = Bck1->getSingleSuccessor();
															const BasicBlock *Bck2_suc = Bck2->getSingleSuccessor();
															if(Bck1_suc != nullptr && Bck2_suc != nullptr && Bck1_suc == Bck2_suc) {
                               

                                BasicBlock *B_Phi = *succ_begin(Bck1);
                                if (!(B_Phi->empty())) {
                                  if (PHINode *phi =
                                          dyn_cast<PHINode>(B_Phi->begin())) {
                                    bool bool1 = check_incoming1(
                                        phi, &index_reg, ty, one, eight);
                                    bool bool2 =
                                        check_incoming2(phi, &add_five, ty,
                                                        forty_eight, bit_inst2);
                                    // This checking is done because if a
                                    // function
                                    // is just forwarding the va_list, then
                                    // there
                                    // will be no incoming phi node, resulting
                                    // in a
                                    // crash
                                    if (bool1 && bool2) {
				


                                      if (phi->getType()
                                              ->getPointerElementType()
                                              ->isIntegerTy()) {
                                        bit_width =
                                            phi->getType()
                                                ->getPointerElementType()
                                                ->getIntegerBitWidth();

                                      } else if (phi->getType()
                                                     ->getPointerElementType()
                                                     ->isFloatingPointTy()) {
                                        bit_width = 130;

                                      } else if (phi->getType()
                                                     ->getPointerElementType()
                                                     ->isPointerTy()) {
                                        bit_width = 0;

                                      } else

                                        bit_width = 140;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
																	char p_array[100];
																				std::sprintf(p_array, F->getName().str().c_str());
																								                int maxLimit = 100;
																						int arrayLimit = std::strlen(p_array);
																	//errs()<< p_array << "\n";
																			int count = 0;					   
																			ArrayType* NArrayTy_0 = ArrayType::get(IntegerType::get(*Context, 8), maxLimit);
																			GlobalVariable* pngvar_array_str = new GlobalVariable(*N_M,
																					NArrayTy_0,
																					false,
																					GlobalValue::InternalLinkage,
																					0, // has initializer, specified below
																					"test_global");
																			std::vector<Constant*> classpnum_elems;
																			Constant *const_array_6;
																			count = 0;
																			for(int t=0; t<arrayLimit; t++) {
																				count += 1;
																				const_array_6 = ConstantInt::get(Type::getInt8Ty(*Context), p_array[t], true);
																				classpnum_elems.push_back(const_array_6);
																			}
																			for(int t=count+1; t<=maxLimit; t++) {
																				const_array_6 = ConstantInt::get(Type::getInt8Ty(*Context), 0, true);
																				classpnum_elems.push_back(const_array_6);
																			}
																			Constant* classpnum_array_1 = ConstantArray::get(NArrayTy_0, classpnum_elems);
																			pngvar_array_str->setInitializer(classpnum_array_1);
////////////////////////////////////////////////////////////////////////////////////////

                                      IRBuilder<> builder(phi->getNextNode());
                                      PHINode *pnode = builder.CreatePHI(ty, 2);
                                      pnode->addIncoming(index_reg, Bck1);
                                      pnode->addIncoming(add_five, Bck2);
                                      IRBuilder<> Builder(
                                          (phi->getNextNode())->getNextNode());
                                      Value *Param[] = {
                                          Builder.CreatePointerCast(pngvar_array_str, Int8PtrTy),
                                          pnode,
                                          ConstantInt::get(Int64Ty, bit_width)};
			

                                      Constant *GCOVInit =
                                          N_M->getOrInsertFunction(
                                              "check_index", VoidTy, Int8PtrTy, ty,
                                              Int64Ty, nullptr);
                                      Builder.CreateCall(GCOVInit, Param);
                                      counter++;
                                      flag_va_intrinsic++;

                                    } // check for the incoming phi ends
                                    else {
																			// errs() << "phi incoming null \n"; // FIXME
																			FILE *fp;
																			fp = fopen("/home/priyam/up_llvm/data_m/err_mozilla.txt", "a+");
																			fprintf(fp, "-------------------------------------------------\n");
																			fprintf(fp, "Error: PHI incoming null \n");
																			fclose(fp);

																		}
                                    //FIXME 
                                    // How to handle the case where it forwards
                                    // the
                                    // list
                                  }
                                }
                              }
                            }
                          }
                        } else {
                          for (User *ur : urv->users())
                            queue_user.push(ur);
                        }
                        if (flag_while_brk == 1)
                          break;
                      }
                    }
                  }
                }
              }

            } // for call_inst
            // }
          }
        } // for va_start_count
      }
    }
    return false;
  }

  virtual bool runOnFunction(Function &F) { return false; }
};

// Two checker functions of the pHI node

bool VASAN::check_incoming1(PHINode *phi, Value **index_reg, Type *ty,
                            Value *one, Value *eight) {

  Value *index = 0;
  if (BitCastInst *gp = dyn_cast<BitCastInst>((phi->getIncomingValue(0)))) {
    if (GetElementPtrInst *gpns =
            dyn_cast<GetElementPtrInst>((gp->getOperand(0)))) {
      index = gp->getOperand(0);
      IRBuilder<> Builder(gp->getNextNode());
      *index_reg = Builder.CreateZExt(gpns->getOperand(1), ty);
      auto eight_zext = Builder.CreateZExt(eight, ty);

      auto one_zext = Builder.CreateZExt(one, ty);
    } else
      return false;
  } else
    return false;

  return true;
}
///
bool VASAN::check_incoming2(PHINode *phi, Value **add_five, Type *ty,
                            Value *forty_eight, Value *bit_inst2) {

  Value *index2 = 0;
  Value *base;
  Value *diff1 = 0;

  if (BitCastInst *bp = dyn_cast<BitCastInst>((phi->getIncomingValue(1)))) {
    if (LoadInst *gpns2 = dyn_cast<LoadInst>((bp->getOperand(0)))) {
      if (GetElementPtrInst *gpns3 =
              dyn_cast<GetElementPtrInst>((gpns2->getOperand(0)))) {

        index2 = gpns3->getOperand(1);
        IRBuilder<> Builder(bp);
        auto bit_inst = Builder.CreatePtrToInt(bp->getOperand(0), ty);

        diff1 = Builder.CreateSub(bit_inst, bit_inst2);

        auto forty_eight_ty = Builder.CreateZExt(forty_eight, ty);
        *add_five = Builder.CreateAdd(diff1, forty_eight_ty);
      }
    } else
      return false;
  } else
    return false;
  return true;
}
}

// register pass
char VASAN::ID = 0;

INITIALIZE_PASS(VASAN, "VASAN", "VASAN", false, false);

ModulePass *llvm::createVASANPass() { return new VASAN(); }
