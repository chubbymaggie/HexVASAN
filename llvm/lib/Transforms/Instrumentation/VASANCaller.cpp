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
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>

int counter = 1;
// extern std::map<std::string, boost::uuids::uuid> variadic_map;
extern std::map<llvm::Value *, long int> variadic_map;


using namespace llvm;
using std::string;

namespace {

struct VASANCaller : public ModulePass {

  static char ID;
  VASANCaller() : ModulePass(ID) {}

  bool doInitialization(Module &M) { return true; }

  bool doFinalization(Module &M) { return false; }

  virtual bool runOnModule(Module &M) {

    Module *N_M;
    N_M = &M;
    LLVMContext &Ctx = M.getContext();

    srand(time(0));

    Type *VoidTy = Type::getVoidTy(Ctx);
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *Int8Ty = Type::getInt8Ty(Ctx);
    Type *Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
    Type *Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
    Type *Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));

    Constant *ty_i8 = ConstantInt::get(Type::getInt64Ty(Ctx), 8);
    Constant *ty_i16 = ConstantInt::get(Type::getInt64Ty(Ctx), 16);
    Constant *ty_i32 = ConstantInt::get(Type::getInt64Ty(Ctx), 32);
    Constant *ty_i64 = ConstantInt::get(Type::getInt64Ty(Ctx), 64);
    Constant *ty_i28 = ConstantInt::get(Type::getInt64Ty(Ctx), 128);
    Constant *ty_float = ConstantInt::get(Type::getInt64Ty(Ctx), 130);
    Constant *ty_default = ConstantInt::get(Type::getInt64Ty(Ctx), 140);
    Constant *ty_pointer = ConstantInt::get(Type::getInt64Ty(Ctx), 0);

    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
      std::ofstream f_callsite;
      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
        BasicBlock &b = *BB;
        for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

          if (CallInst *call_inst = dyn_cast<CallInst>(&*i)) {

            if ((call_inst->getCalledFunction()) == nullptr) {

              if (PointerType *Ty = dyn_cast<PointerType>(
                      ((call_inst->getCalledValue())->getType()))) {
                if (FunctionType *FT =
                        dyn_cast<FunctionType>(Ty->getPointerElementType())) {
                  if ((FT->isVarArg())) {
                    //===============================================
                    // Constant *id =ConstantInt::get(Type::getInt32Ty(Ctx),
                    // rand());
                    Constant *id =
                        ConstantInt::get(Type::getInt64Ty(Ctx), rand());
                    
                    std::string str;
                    llvm::raw_string_ostream rso(str);

                    if (MDNode *md = call_inst->getMetadata("dbg")) {
                      if (DILocation *dl = dyn_cast<DILocation>(md)) {
                        auto line_no = dl->getLine();
                        // auto col_no = dl->getCol();
                        std::string file_name = dl->getFilename();

                        call_inst->getFunctionType()->print(rso);
                        std::string pathname =
                            "/home/priyam/up_llvm/data_m/callsite/" +
                            file_name + "_csite.tsv"; //FIXME
                        f_callsite.open(pathname,
                                        std::ios_base::app |
                                            std::ios_base::out); // FIXME the
                                                                 // path needs
                                                                 // to be fixed

                        f_callsite << "0"
                                   << "\t ---------------"
                                   << "\t" << rso.str() << "\t indirect \t"
                                   << call_inst->getNumArgOperands() << "\t"
                                   << line_no << "\t" << file_name << "\t"
                                   << "\n";
                      }
                    }

                    //================================================

                    ArrayType *arr_type = ArrayType::get(
                        Int64Ty,
                        (call_inst->getNumArgOperands())); // FIXME:  changed
                                                           // here the numparams
                    std::vector<Constant *> arg_types;
                    int i = 1;
                    /* The following loop saves the types of the arguments,
                     * except the
                     * fixed parameters, and FType->getNumParams gives the
                     * number of
                     * fixed parameters */
                    for (Value *arg_value : call_inst->arg_operands()) {

                      if (arg_value->getType()->isPointerTy()) {
                        arg_types.push_back(ty_pointer);
                      } else if (arg_value->getType()->isFloatingPointTy()) {
                        arg_types.push_back(ty_float);
                      } else if (arg_value->getType()->isIntegerTy()) {
                        if (arg_value->getType()->getIntegerBitWidth() == 128) {
                          arg_types.push_back(ty_i28);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   64) {

                          arg_types.push_back(ty_i64);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   32) {

                          arg_types.push_back(ty_i32);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   16) {

                          arg_types.push_back(ty_i16);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   8) {
                          arg_types.push_back(ty_i8);
                        } else
                          arg_types.push_back(ty_default);
                      }
                      // }
                      i++;
                    }

                    Constant *arg_c =
                        ConstantInt::get(Type::getInt32Ty(Ctx),
                                         ((call_inst->getNumArgOperands())));
                    Constant *Init_array =
                        ConstantArray::get(arr_type, arg_types);
                    GlobalVariable *type_array = new GlobalVariable(
                        M, arr_type, true, GlobalValue::InternalLinkage,
                        Init_array, "Type_Array");

                    auto struct_ty = llvm::StructType::create(
                        Ctx, {Int64Ty, Int32Ty, Int64PtrTy}); //FIXME
										/*auto struct_ty = llvm::StructType::create(
                        Ctx, {Int32Ty, Int64PtrTy});*/
                    GlobalVariable *struct_node = new GlobalVariable(
                        M, struct_ty, true, GlobalValue::InternalLinkage,
                        nullptr, "Struct_variable");
                    Constant *array_ty_int =
                        ConstantExpr::getPointerCast(type_array, Int64PtrTy);
                    struct_node->setInitializer(ConstantStruct::get(
                        struct_ty, {id, arg_c, array_ty_int})); //FIXME

										/*struct_node->setInitializer(ConstantStruct::get(
                        struct_ty, {arg_c, array_ty_int}));*/

                    IRBuilder<> Builder(call_inst);
                    Value *Param[] = {struct_node};
                    Constant *GInit = N_M->getOrInsertFunction(
                        "info_push", VoidTy, struct_node->getType(), nullptr);
                    Builder.CreateCall(GInit, Param);

                    int value = 0;
                    IRBuilder<> builder(call_inst->getNextNode());
                    Value *Param2 = {ConstantInt::get(Int32Ty, value)};
                    Constant *GFin = N_M->getOrInsertFunction(
                        "info_pop", VoidTy, Int32Ty, nullptr);
                    builder.CreateCall(GFin, Param2);

                    /// should end here
                  }
                }
              }
            } // getcalled nullptr ends here
            else {
              if (PointerType *Ty = dyn_cast<PointerType>(
                      ((call_inst->getCalledValue())->getType()))) {
                if (FunctionType *FT =
                        dyn_cast<FunctionType>(Ty->getPointerElementType())) {
                  if ((FT->isVarArg())) {
                    //========================================================================================
                    Function *callee = call_inst->getCalledFunction();
                    // Constant *id =ConstantInt::get(Type::getInt32Ty(Ctx),
                    // rand());
                    Constant *id =
                        ConstantInt::get(Type::getInt64Ty(Ctx), rand());

                    std::string str;
                    llvm::raw_string_ostream rso(str);

                    if (MDNode *md = call_inst->getMetadata("dbg")) {
                      if (DILocation *dl = dyn_cast<DILocation>(md)) {
                        auto line_no = dl->getLine();
                        // auto col_no = dl->getCol();
                        std::string file_name = dl->getFilename();

                        callee->getFunctionType()->print(rso);
                        std::string pathname =
                            "/home/priyam/up_llvm/data_m/callsite/" +
                            file_name + "_csite.tsv";
                        f_callsite.open(pathname, std::ios_base::app |
                                                      std::ios_base::out);

                        f_callsite << variadic_map.find(callee)->second << "\t"
                                   << callee->getName().str() << "\t"
                                   << rso.str() << "\t direct \t"
                                   << call_inst->getNumArgOperands() << "\t "
                                   << line_no << "\t" << file_name << "\t"
                                   << "\n";
                        // f_callsite.close();
                      }
                    }

                    //========================================================================================

                    ArrayType *arr_type = ArrayType::get(
                        Int64Ty, (call_inst->getNumArgOperands()));
                    std::vector<Constant *> arg_types;
                    int i = 1;
                    /* The following loop saves the types of the arguments,
                     * except the
                     * fixed parameters, and FType->getNumParams gives the
                     * number of
                     * fixed parameters */
                    for (Value *arg_value : call_inst->arg_operands()) {

                      // if (i > FT->getNumParams()) { //FIXME

                      if (arg_value->getType()->isPointerTy()) {
                        arg_types.push_back(ty_pointer);
                      } else if (arg_value->getType()->isFloatingPointTy()) {
                        arg_types.push_back(ty_float);
                      } else if (arg_value->getType()->isIntegerTy()) {
                        if (arg_value->getType()->getIntegerBitWidth() == 128) {
                          arg_types.push_back(ty_i28);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   64) {

                          arg_types.push_back(ty_i64);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   32) {

                          arg_types.push_back(ty_i32);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   16) {

                          arg_types.push_back(ty_i16);
                        } else if (arg_value->getType()->getIntegerBitWidth() ==
                                   8) {
                          arg_types.push_back(ty_i8);
                        } else
                          arg_types.push_back(ty_default);
                      }
                      //}
                      i++;
                    }

                    Constant *arg_c =
                        ConstantInt::get(Type::getInt32Ty(Ctx),
                                         ((call_inst->getNumArgOperands())));
                    Constant *Init_array =
                        ConstantArray::get(arr_type, arg_types);
                    GlobalVariable *type_array = new GlobalVariable(
                        M, arr_type, true, GlobalValue::InternalLinkage,
                        Init_array, "Type_Array");

                    auto struct_ty = llvm::StructType::create(
                        Ctx, {Int64Ty, Int32Ty, Int64PtrTy}); // FIXME id need to taken care of 

										/*auto struct_ty = llvm::StructType::create(
                        Ctx, {Int32Ty, Int64PtrTy});*/

                    GlobalVariable *struct_node = new GlobalVariable(
                        M, struct_ty, true, GlobalValue::InternalLinkage,
                        nullptr, "Struct_variable");
                    Constant *array_ty_int =
                        ConstantExpr::getPointerCast(type_array, Int64PtrTy);
                    struct_node->setInitializer(ConstantStruct::get(
                        struct_ty, {id, arg_c, array_ty_int}));  // FIXME id need to taken care of 
										/*struct_node->setInitializer(ConstantStruct::get(
                        struct_ty, {arg_c, array_ty_int}));*/

                    IRBuilder<> Builder(call_inst);
                    Value *Param[] = {struct_node};
                    Constant *GInit = N_M->getOrInsertFunction(
                        "info_push", VoidTy, struct_node->getType(), nullptr);
                    Builder.CreateCall(GInit, Param);

                    int value = 0;
                    IRBuilder<> builder(call_inst->getNextNode());
                    Value *Param2 = {ConstantInt::get(Int32Ty, value)};
                    Constant *GFin = N_M->getOrInsertFunction(
                        "info_pop", VoidTy, Int32Ty, nullptr);
                    builder.CreateCall(GFin, Param2);

                    /// should end here
                  }
                }
              }
            }
          }
        }
      }
      f_callsite.close();
    }

    return false;
  }

  virtual bool runOnFunction(Function &F) { return false; }
};
}

// register pass
char VASANCaller::ID = 0;

INITIALIZE_PASS(VASANCaller, "VASANCaller", "VASANCaller", false, false);

ModulePass *llvm::createVASANCallerPass() { return new VASANCaller(); }
