#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/MemoryBuiltins.h"
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

#define MAXPATH 1000

int counter = 1;
extern std::map<llvm::Value *, long int> variadic_map;

using namespace llvm;
using std::string;

namespace
{
	// =====  Hash Calculation Function ==============
	uint64_t hashing(uint64_t OldHash, uint64_t NewData) {

		NewData = OldHash + NewData;
		return NewData;
	}
	
	// =====  Hash Calculation Function ==============
	uint64_t hashType(Type *T, Value *V) {

		uint64_t Result = 0;
		if (LoadInst *dl = dyn_cast<LoadInst>(V)) {
			if (GetElementPtrInst *gepinst =
				dyn_cast<GetElementPtrInst>((dl->getOperand(0)))) {
				if (BitCastInst *binst = dyn_cast<BitCastInst>(gepinst->getOperand(0))) {

					if (binst->getOperand(0)
						->getType()
						->getPointerElementType()
						->isStructTy()) {
						Result = 13;
						return Result;
					}
				}
			}
		}

		if (T->getTypeID() == 15) {

			if (T->getPointerElementType()) {
				if (T->getPointerElementType()->getTypeID() == 13) {

					Result = 13;
					return Result;
				}

			}
			Result = 15;
			return Result;

		} else {
			Result = hashing(Result, T->getTypeID());

			if (T->isIntegerTy()) {
				Result = hashing(Result, T->getIntegerBitWidth());
			}
			if (T->isFloatingPointTy()) {
				Result = hashing(Result, T->getFPMantissaWidth());
			}
		}
		return Result;
	}
}

namespace llvm
{
	struct VASANCallerVisitor : public InstVisitor<VASANCallerVisitor>
	{
	public:
		VASANCallerVisitor(Module& mod) : M(mod), Ctx(mod.getContext())
		{
			VoidTy = Type::getVoidTy(Ctx);
			Int64Ty = Type::getInt64Ty(Ctx);
			Int32Ty = Type::getInt32Ty(Ctx);			
			Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));
			file_rand = rand();
			file_r  = std::to_string(file_rand);
//			M.dump();
		}

		template <typename InstType> void handleInst(InstType& I)
		{
			auto getcallvalue = I.getCalledValue();

			bool indirect = false;

            while (auto bitcst = dyn_cast<ConstantExpr>(getcallvalue)) {
              if (bitcst->isCast()) {
                getcallvalue = bitcst->getOperand(0);
              }
            }
            if (isa<Function>(getcallvalue)) {
              indirect = false;
            } else
              indirect = true;

            auto *getft = cast<PointerType>(getcallvalue->getType());
            FunctionType *FT =
                cast<FunctionType>(getft->getPointerElementType());

            if ((FT->isVarArg())) {
              uint64_t random = rand();
              Constant *id = ConstantInt::get(Type::getInt64Ty(Ctx), random);

//			  errs() << "Found Vararg Call:\n";
//			  I.dump();

              std::string str;
              llvm::raw_string_ostream rso(str);
              unsigned line_no;
              std::string file_name;
              if (MDNode *md = I.getMetadata("dbg")) {
                if (DILocation *dl = dyn_cast<DILocation>(md)) {
                  line_no = dl->getLine();
                  file_name = dl->getFilename();
                }
              }
			  
              if (getenv("VASAN_C_LOG_PATH") != nullptr) {

                char *home = getenv("VASAN_C_LOG_PATH");

                I.getFunctionType()->print(rso);
                std::string pathname = home + file_r + "callsite.csv";
                std::ofstream f_callsite;
                f_callsite.open(pathname,
                                std::ios_base::app | std::ios_base::out);
                std::string _dir;
                if (indirect) {
                  _dir = "indirect";
                } else {
                  _dir = "direct";
                }

                f_callsite << random << "\t ---------------"
                           << "\t" << rso.str() << "\t" << _dir << "\t"
                           << I.getNumArgOperands() << "\t" << line_no
                           << "\t" << file_name << "\t"
                           << "\n";

                f_callsite.close();
              }

              //================================================
              FunctionType *FTypee = I.getFunctionType();
              ArrayType *arr_type =
                  ArrayType::get(Int64Ty, (I.getNumArgOperands() -
                                           FTypee->getNumParams()));

              std::vector<Constant *> arg_types;
              unsigned i = 1;
              uint64_t result_hash = 0;
              for (Value *arg_value : I.arg_operands()) {
                if (i > (FTypee->getNumParams())) {
                  result_hash = hashType(arg_value->getType(), arg_value);
                  // errs() << "Caller: Resulting Hash is " << result_hash <<
                  // "\n";
                  Constant *ty_val =
                      ConstantInt::get(Type::getInt64Ty(Ctx), result_hash);
                  arg_types.push_back(ty_val);
                }
                i++;
              }

              Constant *arg_c = ConstantInt::get(
                  Type::getInt64Ty(Ctx), ((I.getNumArgOperands()) -
                                          (FTypee->getNumParams())));
              Constant *Init_array = ConstantArray::get(arr_type, arg_types);
              GlobalVariable *type_array = new GlobalVariable(
                  M, arr_type, true, GlobalValue::InternalLinkage, Init_array,
                  "Type_Array");

              auto struct_ty = llvm::StructType::create(
                  Ctx, {Int64Ty, Int64Ty, Int64PtrTy}); // FIXME

              GlobalVariable *struct_node = new GlobalVariable(
                  M, struct_ty, true, GlobalValue::InternalLinkage, nullptr,
                  "Struct_variable");
              Constant *array_ty_int =
                  ConstantExpr::getPointerCast(type_array, Int64PtrTy);
              struct_node->setInitializer(ConstantStruct::get(
                  struct_ty, {id, arg_c, array_ty_int})); // FIXME
			  
              IRBuilder<> Builder(&I);
              Value *Param[] = {struct_node};
              Constant *GInit = M.getOrInsertFunction(
                  "__vasan_info_push", VoidTy, struct_node->getType(), nullptr);
              Builder.CreateCall(GInit, Param);

			  int value = 0;
			  Value *Param2 = {ConstantInt::get(Int32Ty, value)};
			  Constant *GFin = M.getOrInsertFunction("__vasan_info_pop", VoidTy,
													 Int32Ty, nullptr);

			  if (dyn_cast<InvokeInst>(&I))
			  {
				  InvokeInst* Invoke = dyn_cast<InvokeInst>(&I);
				  // Instrument landing pad instead
				  Builder.SetInsertPoint(Invoke->getSuccessor(1)->getFirstNonPHI()->getNextNode());
				  Builder.CreateCall(GFin, Param2);
			  }
			  else
			  {

				  Builder.SetInsertPoint(I.getNextNode());
				  Builder.CreateCall(GFin, Param2);
			  }
            }
		}

		void visitCallInst(CallInst& I)
		{
			handleInst<CallInst>(I);
		}

		void visitInvokeInst(InvokeInst& I)
		{
			handleInst<InvokeInst>(I);
		}

		Module& M;
		LLVMContext &Ctx;
		Type *VoidTy;
		Type *Int64Ty;
		Type *Int32Ty;	   
		Type *Int64PtrTy;
		uint32_t file_rand;	   
		std::string file_r;
	};
}


namespace {

struct VASANCaller : public ModulePass {

  static char ID;
  VASANCaller() : ModulePass(ID) {}

  bool doInitialization(Module &M) { return true; }

  bool doFinalization(Module &M) { return false; }
  virtual bool runOnModule(Module &M) {
    srand(time(0));

	VASANCallerVisitor V(M);
	V.visit(M);

    return false;
  }

  virtual bool runOnFunction(Function &F) { return false; }
};
}

// register pass
char VASANCaller::ID = 0;

INITIALIZE_PASS(VASANCaller, "VASANCaller", "VASANCaller", false, false)

ModulePass *llvm::createVASANCallerPass() { return new VASANCaller(); }
