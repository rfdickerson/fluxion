#pragma once

#include "typecheck.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace fluxion {

struct GeneratedModule {
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
};

class LlvmCodeGen {
 public:
  explicit LlvmCodeGen(const CheckedProgram& program);
  GeneratedModule generate();
  std::string emit_ir();

 private:
  struct Local {
    llvm::AllocaInst* storage = nullptr;
    TypeRef type;
  };

  void declare_runtime();
  void declare_records();
  void declare_functions();
  void define_functions();
  llvm::Type* llvm_type(const TypeRef& type);
  llvm::StructType* record_type(const std::string& name);
  llvm::Function* function(const std::string& name);
  llvm::Value* codegen_expr(Expr& expr, std::unordered_map<std::string, Local>& locals);
  llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name);
  llvm::Value* cast_numeric(llvm::Value* value, const TypeRef& from, const TypeRef& to);
  int field_index(const std::string& record, const std::string& field, const SourceLocation& loc) const;
  const RecordDecl& record_decl(const std::string& name) const;

  const CheckedProgram& program_;
  std::unique_ptr<llvm::LLVMContext> context_;
  std::unique_ptr<llvm::Module> module_;
  std::unique_ptr<llvm::IRBuilder<>> builder_;
  std::unordered_map<std::string, llvm::StructType*> record_types_;
  std::unordered_map<std::string, llvm::Function*> functions_;
};

}  // namespace fluxion
