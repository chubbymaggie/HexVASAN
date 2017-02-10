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

namespace {

struct VASANCaller : public ModulePass {

  static char ID;
  VASANCaller() : ModulePass(ID) {}

  bool doInitialization(Module &M) { return true; }

  bool doFinalization(Module &M) { return false; }
  uint64_t hashType(Type *T, Value *V);

  uint64_t hashing(uint64_t OldHash, uint64_t NewData);
  uint32_t file_rand = rand();

  std::string file_r = std::to_string(file_rand);
  virtual bool runOnModule(Module &M) {

    Module *N_M;
    N_M = &M;
    LLVMContext &Ctx = M.getContext();

    srand(time(0));

    Type *VoidTy = Type::getVoidTy(Ctx);
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    Type *Int32Ty = Type::getInt32Ty(Ctx);

    Type *Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));

    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {

      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
        BasicBlock &b = *BB;
        for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

          if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {
            bool indirect = false;

            auto getcallvalue = call_inst->getCalledValue();
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

              std::string str;
              llvm::raw_string_ostream rso(str);
              unsigned line_no;
              std::string file_name;
              if (MDNode *md = call_inst->getMetadata("dbg")) {
                if (DILocation *dl = dyn_cast<DILocation>(md)) {
                  line_no = dl->getLine();
                  file_name = dl->getFilename();
                }
              }
              if (getenv("VASAN_C_LOG_PATH") != nullptr) {

                char *home = getenv("VASAN_C_LOG_PATH");

                call_inst->getFunctionType()->print(rso);
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
                           << call_inst->getNumArgOperands() << "\t" << line_no
                           << "\t" << file_name << "\t"
                           << "\n";

                f_callsite.close();
              }

              //================================================
              FunctionType *FTypee = call_inst->getFunctionType();
              ArrayType *arr_type =
                  ArrayType::get(Int64Ty, (call_inst->getNumArgOperands() -
                                           FTypee->getNumParams()));

              std::vector<Constant *> arg_types;
              int i = 1;
              uint64_t result_hash = 0;
              for (Value *arg_value : call_inst->arg_operands()) {
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
                  Type::getInt64Ty(Ctx), ((call_inst->getNumArgOperands()) -
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

              IRBuilder<> Builder(call_inst);
              Value *Param[] = {struct_node};
              Constant *GInit = N_M->getOrInsertFunction(
                  "info_push", VoidTy, struct_node->getType(), nullptr);
              Builder.CreateCall(GInit, Param);

              int value = 0;
              IRBuilder<> builder(call_inst->getNextNode());
              Value *Param2 = {ConstantInt::get(Int32Ty, value)};
              Constant *GFin = N_M->getOrInsertFunction("info_pop", VoidTy,
                                                        Int32Ty, nullptr);
              builder.CreateCall(GFin, Param2);
            }
          }
        }
      }
    }

    return false;
  }

  virtual bool runOnFunction(Function &F) { return false; }
};
// =====  Hash Calculation Function ==============

uint64_t VASANCaller::hashType(Type *T, Value *V) {

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

    } else {
      Result = 15;
      return Result;
    }

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

// =====  Hash Calculation Function ==============
uint64_t VASANCaller::hashing(uint64_t OldHash, uint64_t NewData) {

  NewData = OldHash + NewData;
  return NewData;
}
}

// register pass
char VASANCaller::ID = 0;

INITIALIZE_PASS(VASANCaller, "VASANCaller", "VASANCaller", false, false);

ModulePass *llvm::createVASANCallerPass() { return new VASANCaller(); }
