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
#include "llvm/IR/TypeBuilder.h"
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
#include <map>
#include <queue>
#include <stdio.h>
#include <string.h>
#include <string>

std::map<llvm::AllocaInst *, llvm::AllocaInst *> list_map;
std::map<llvm::Value *, long int> variadic_map;
using namespace llvm;
using std::string;

namespace llvm
{
	struct VASANVisitor : public InstVisitor<VASANVisitor>
	{
	public:
		VASANVisitor(Module& M) : N_M(M) {}

		void instrumentVAArgs();
		
		void visitCallInst(CallInst& I)
		{
			Function *ft = I.getCalledFunction();

			if (ft == nullptr)
				return;

			auto ID = ft->getIntrinsicID();
			if (ID != Intrinsic::vastart &&
				ID != Intrinsic::vaend &&
				ID != Intrinsic::vacopy)
				return;

			// Insert a call after the vararg func
			IRBuilder<> B(I.getNextNode());
			Type *VoidTy = Type::getVoidTy(N_M.getContext());
			Type* valistPtr = PointerType::getUnqual(Type::getInt8Ty(N_M.getContext()));

			if (ft->getIntrinsicID() == llvm::Intrinsic::vastart)
			{
				// The first argument of the call is a bitcast
				// of the va_list struct to i8*
				BitCastInst* listCast = dyn_cast<BitCastInst>(I.getArgOperand(0));

				if (!listCast)
					return;

				Value* listPtr = listCast->getOperand(0);
				if (listPtr->getType() != valistPtr)
					listPtr = B.CreateBitCast(listPtr, valistPtr);
				
				Constant* Func = N_M.getOrInsertFunction("__vasan_vastart", VoidTy, valistPtr, nullptr);
				B.CreateCall(Func, {listPtr});
			}
			else if (ft->getIntrinsicID() == llvm::Intrinsic::vacopy)
			{				
				// arg0 of the intrinsic is the dst
				// arg1 of the intrinsic is the src
				// the VASAN runtime does it the other way around
				BitCastInst* dstCast = dyn_cast<BitCastInst>(I.getArgOperand(0));
				BitCastInst* srcCast = dyn_cast<BitCastInst>(I.getArgOperand(1));			   

				if (!srcCast || !dstCast)
					return;

				Value* dstPtr = dstCast->getOperand(0);
				Value* srcPtr = srcCast->getOperand(0);
				if (srcPtr->getType() != valistPtr)
					srcPtr = B.CreateBitCast(srcPtr, valistPtr);
				if (dstPtr->getType() != valistPtr)
					dstPtr = B.CreateBitCast(dstPtr, valistPtr);

				Constant* Func = N_M.getOrInsertFunction("__vasan_vacopy", VoidTy, valistPtr, valistPtr, nullptr);
				B.CreateCall(Func, {srcPtr, dstPtr});
			}
			else if (ft->getIntrinsicID() == llvm::Intrinsic::vaend)
			{
				BitCastInst* listCast = dyn_cast<BitCastInst>(I.getArgOperand(0));

				if (!listCast)
					return;

				Value* listPtr = listCast->getOperand(0);
				if (listPtr->getType() != valistPtr)
					listPtr = B.CreateBitCast(listPtr, valistPtr);
				
				Constant* Func = N_M.getOrInsertFunction("__vasan_vaend", VoidTy, valistPtr, nullptr);
				B.CreateCall(Func, {listPtr});
			}
		}

		void visitVAArgInstr(VAArgInst& I)
		{
			// FreeBSD clang emits these afaik
			errs() << "Saw VA Arg Inst: ";
			I.dump();
		}

		Module& N_M;
	};
}

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
  uint64_t hashType(Type *T);
  uint64_t hashing(uint64_t OldHash, uint64_t NewData);

  uint32_t file_rand = rand();
  std::string file_r = std::to_string(file_rand);
	
	virtual bool runOnModule(Module &M) {

		int counter = 0;
		Module *N_M;
		N_M = &M;
		LLVMContext &Ctx = M.getContext();
		Context = &M.getContext();

		Type *VoidTy = Type::getVoidTy(Ctx);
		Type *Int64Ty = Type::getInt64Ty(Ctx);
		Type *Int32Ty = Type::getInt32Ty(Ctx);
		Type *Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
		Type *Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));

		auto dm = M.getDataLayout();
		auto ty = dm.getIntPtrType(Ctx);
		Value *index_arg = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
		srand(time(nullptr));
		std::string file_name;

		for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
			std::ofstream func_va;
			Value *funcptr;

			if (Function *Fnew = dyn_cast<Function>(F)) {
				funcptr = dyn_cast<Value>(Fnew);
			}
			std::string addrtaken = "no";
			std::string definition = "definition";
			if (F->empty()) {
				definition = "declaration";
			} else
				definition = "definition";

			if (F->isVarArg()) {

				/*================================================*/
				uint32_t user_count = 0;
				uint32_t user_call_count = 0;

				for (User *func_users : F->users()) {
					user_count++;

					ConstantExpr *bc = dyn_cast<ConstantExpr>(func_users);
					while (bc != nullptr && bc->isCast()) {
						func_users = *bc->users().begin();
						bc = dyn_cast<ConstantExpr>(func_users);
					}

					if (CallInst *cist = dyn_cast<CallInst>(func_users)) {
						user_call_count++;
					}
				}
				if (user_count == user_call_count) {
					addrtaken = "no";
				} else {
					addrtaken = "yes";
				}

				/*================================================*/

				long int unique_id = rand();
				variadic_map.insert(
					std::pair<llvm::Value *, long int>(funcptr, unique_id));
				std::string str;
				llvm::raw_string_ostream rso(str);
				F->getFunctionType()->print(rso);
				std::queue<User *> func_user;
				uint32_t line_no;

				if (MDNode *md = F->getMetadata("dbg")) {
					if (DISubprogram *dl = dyn_cast<DISubprogram>(md)) {
						line_no = dl->getLine();
						file_name = dl->getFilename();
					}
				}

				if (getenv("VASAN_LOG_PATH") != nullptr) {
					char *home = getenv("VASAN_LOG_PATH");

					std::string pathname = home + file_r + "vfunc.csv";

					func_va.open(pathname, std::ios_base::app | std::ios_base::out);

					func_va << unique_id << "\t" << F->getName().str() << "\t"
							<< rso.str() << "\t" << F->getLinkage() << "\t" << file_name
							<< "\t" << line_no;

					func_va << "\t" << addrtaken << "\t" << definition << "\n";
				}

				for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
					BasicBlock &b = *BB;
					for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

						if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

							Function *ft = call_inst->getCalledFunction();

							if ((ft != nullptr) &&
								((ft->getIntrinsicID() == llvm::Intrinsic::vastart))) {

								IRBuilder<> Builder(call_inst);
								Value *Param[] = {ConstantInt::get(Int64Ty, unique_id)};
								Constant *GInit = N_M->getOrInsertFunction("__vasan_assign_id", VoidTy,
																		   Int64Ty, nullptr);
								Builder.CreateCall(GInit, Param);
							}
						}
					}
				}
			}
			func_va.close();
		}
		//================csv information ends here
		VASANVisitor V(M);
		V.visit(M);
		
		//=============================================================
/*
for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {

      FunctionType *FT = F->getFunctionType();
      Value *bit_inst2 = 0;

      int flag_va_intrinsic = 0;

      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
        BasicBlock &b = *BB;
        for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

          int bit_width = 0;
          Value *index_reg = 0;
          Value *base;
          Value *add_five = 0;

          Value *zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
          Value *one = ConstantInt::get(Type::getInt32Ty(Ctx), 1);
          Value *eight = ConstantInt::get(Type::getInt32Ty(Ctx), 8);
          Value *forty_eight = ConstantInt::get(Type::getInt32Ty(Ctx), 48);

          Value *Idxs[] = {ConstantInt::get(Int32Ty, 0),
                           ConstantInt::get(Int32Ty, 2)};
          if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

            Function *ft = call_inst->getCalledFunction();

            if ((ft != nullptr) &&
                ((ft->getIntrinsicID() == llvm::Intrinsic::vastart) ||
                 (ft->getIntrinsicID() == llvm::Intrinsic::vaend) ||
                  (ft->getIntrinsicID() == llvm::Intrinsic::vacopy))) {
              if (ft->getIntrinsicID() == llvm::Intrinsic::vastart || (ft->getIntrinsicID() == llvm::Intrinsic::vacopy )) {
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
                          dyn_cast<GetElementPtrInst>((binst->getOperand(0)))) {
                    std::queue<User *> queue_user;

                    AllocaInst *a_inst;
                    if (a_inst = dyn_cast<AllocaInst>(
                            gepinst->getOperand(0))) { // checking for the list
                      int flag_ainst = 0;
                      for (std::map<llvm::AllocaInst *,
                                    llvm::AllocaInst *>::const_iterator it =
                               list_map.begin();
                           it != list_map.end(); it++) {

                        if (it->first == a_inst) {

                          flag_ainst = 1;
                        }
                      }
                      if (flag_ainst == 0) {

                        auto a_alloc = builder.CreateAlloca(Int32Ty);
                        auto store_index = builder.CreateStore(zero, a_alloc);

                        list_map.insert(
                            std::pair<llvm::AllocaInst *, llvm::AllocaInst *>(
                                a_inst, a_alloc));
                      }
                    }
                    for (User *ur : gepinst->getOperand(0)->users())

                      queue_user.push(ur);
                    int flag_while_brk = 0;

                    while ((!queue_user.empty())) {

                      User *urv = queue_user.front();
                      queue_user.pop();

                      if (BranchInst *b = dyn_cast<BranchInst>(urv)) {
                        flag_while_brk = 1;

                        if (b->getSuccessor(0) != nullptr) {
                          BasicBlock *Bck1 = b->getSuccessor(0);
                          if (b->getSuccessor(1) != nullptr) {
                            BasicBlock *Bck2 = b->getSuccessor(1);

                            const BasicBlock *Bck1_suc =
                                Bck1->getSingleSuccessor();
                            const BasicBlock *Bck2_suc =
                                Bck2->getSingleSuccessor();
                            if (Bck1_suc != nullptr && Bck2_suc != nullptr &&
                                Bck1_suc == Bck2_suc) {

                              BasicBlock *B_Phi = *succ_begin(Bck1);
                              if (!(B_Phi->empty())) {
                                if (PHINode *phi =
                                        dyn_cast<PHINode>(B_Phi->begin())) {
                                  bool bool1 = check_incoming1(phi, &index_reg,
                                                               ty, one, eight);
                                  bool bool2 =
                                      check_incoming2(phi, &add_five, ty,
                                                      forty_eight, bit_inst2);
                                  if (bool1 || bool2) {

                                    bit_width =
                                        hashType(phi->getType()
                                                     ->getPointerElementType());
                                    // errs() << "Callee side hash is " <<
                                    // bit_width << "\n";

				    // This fixes bus error in FreeBSD
                                    char *p_array = 
					(char *)malloc(F->getName().size() + file_name.length() + 3);
				    dbgs() << F->getName().str();
                                    std::strcpy(p_array,
                                                 F->getName().str().c_str());
                                    int maxLimit = 100;
                                    int arrayLimit = std::strlen(p_array);
                                    std::strcat(p_array, "::");
                                    std::strcat(p_array, file_name.c_str());
                                    std::string file_name;

                                    int count = 0;
                                    ArrayType *NArrayTy_0 = ArrayType::get(
                                        IntegerType::get(*Context, 8),
                                        maxLimit);
                                    GlobalVariable *pngvar_array_str =
                                        new GlobalVariable(
                                            *N_M, NArrayTy_0, false,
                                            GlobalValue::InternalLinkage,
                                            0, // has initializer, specified
                                               // below
                                            "test_global");
                                    std::vector<Constant *> classpnum_elems;
                                    Constant *const_array_6;
                                    count = 0;
                                    for (int t = 0; t < arrayLimit; t++) {
                                      count += 1;
                                      const_array_6 = ConstantInt::get(
                                          Type::getInt8Ty(*Context), p_array[t],
                                          true);
                                      classpnum_elems.push_back(const_array_6);
                                    }

				    free(p_array);

                                    for (int t = count + 1; t <= maxLimit;
                                         t++) {
                                      const_array_6 = ConstantInt::get(
                                          Type::getInt8Ty(*Context), 0, true);
                                      classpnum_elems.push_back(const_array_6);
                                    }
                                    Constant *classpnum_array_1 =
                                        ConstantArray::get(NArrayTy_0,
                                                           classpnum_elems);
                                    pngvar_array_str->setInitializer(
                                        classpnum_array_1);

                                    if (phi->getNextNode() != nullptr) {

                                      IRBuilder<> builder(phi->getNextNode());

                                      for (std::map<llvm::AllocaInst *,
                                                    llvm::AllocaInst
                                                        *>::const_iterator it =
                                               list_map.begin();
                                           it != list_map.end(); it++) {

                                        if (it->first == a_inst) {

                                          index_arg = it->second;
                                          auto load_second =
                                              builder.CreateLoad(it->second);
                                          auto add_one = builder.CreateAdd(
                                              load_second, one);
                                          auto store_one = builder.CreateStore(
                                              add_one, it->second);
                                        }
                                      }

                                      Value *Param[] = {
                                          builder.CreatePointerCast(
                                              pngvar_array_str, Int8PtrTy),
                                          index_arg,
                                          ConstantInt::get(Int64Ty, bit_width)};

                                      Constant *GCOVInit =
                                          N_M->getOrInsertFunction(
                                              "__vasan_check_index", VoidTy, Int8PtrTy,
                                              Int32PtrTy, Int64Ty, nullptr);
                                      builder.CreateCall(GCOVInit, Param);
                                      counter++;
                                      flag_va_intrinsic++;
                                    }

                                  } // check for the incoming phi ends
                                  else {
                                  }
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
        }
      }
    }
*/
		return false;
	}

	virtual bool runOnFunction(Function &F) { return false; }
};

// =====  Hash Calculation Function ==============

uint64_t VASAN::hashType(Type *T) {

  uint64_t Result = 0;
  Result = hashing(Result, T->getTypeID());
  if (T->isIntegerTy()) {
    Result = hashing(Result, T->getIntegerBitWidth());
  }
  if (T->isFloatingPointTy()) {
    Result = hashing(Result, T->getFPMantissaWidth());
  }

  return Result;
}

// =====  Hash Calculation Function ==============
uint64_t VASAN::hashing(uint64_t OldHash, uint64_t NewData) {

  // FIXME Need to come up with a better hash function
  // NewData = NewData * 2;
  NewData = OldHash + NewData;
  return NewData;
}

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
