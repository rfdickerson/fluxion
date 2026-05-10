#include "codegen_llvm.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace fluxion {

namespace {

bool is_math_f64_unary(const std::string& name) {
  return name == "sin" || name == "cos" || name == "tan" || name == "asin" || name == "acos" ||
         name == "atan" || name == "sqrt" || name == "exp" || name == "log" || name == "log10";
}

double builtin_constant_value(const std::string& name) {
  if (name == "pi") {
    return 3.14159265358979323846;
  }
  if (name == "tau") {
    return 6.28318530717958647692;
  }
  if (name == "e") {
    return 2.71828182845904523536;
  }
  return 0.0;
}

}  // namespace

LlvmCodeGen::LlvmCodeGen(const CheckedProgram& program) : program_(program) {
  context_ = std::make_unique<llvm::LLVMContext>();
  module_ = std::make_unique<llvm::Module>("fluxion", *context_);
  builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
}

GeneratedModule LlvmCodeGen::generate() {
  declare_runtime();
  declare_records();
  declare_functions();
  define_functions();
  return {std::move(context_), std::move(module_)};
}

std::string LlvmCodeGen::emit_ir() {
  auto generated = generate();
  std::string out;
  llvm::raw_string_ostream os(out);
  generated.module->print(os, nullptr);
  return os.str();
}

void LlvmCodeGen::declare_runtime() {
  auto* unit_ty = llvm::Type::getInt32Ty(*context_);
  auto* f64_ty = llvm::Type::getDoubleTy(*context_);
  auto* i32_ty = llvm::Type::getInt32Ty(*context_);
  auto* string_ty = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_));
  auto* matrix_ty = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_));
  functions_["print_f64"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_print_f64", module_.get());
  functions_["print_i32"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {i32_ty}, false), llvm::Function::ExternalLinkage, "fluxion_print_i32", module_.get());
  functions_["print_bool"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {llvm::Type::getInt1Ty(*context_)}, false), llvm::Function::ExternalLinkage, "fluxion_print_bool", module_.get());
  functions_["print_string"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {string_ty}, false), llvm::Function::ExternalLinkage, "fluxion_print_string", module_.get());
  functions_["print_matrix"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {matrix_ty}, false), llvm::Function::ExternalLinkage, "fluxion_print_matrix", module_.get());
  functions_["pow_i32"] = llvm::Function::Create(
      llvm::FunctionType::get(i32_ty, {i32_ty, i32_ty}, false), llvm::Function::ExternalLinkage, "fluxion_pow_i32", module_.get());
  functions_["pow_f64"] = llvm::Function::Create(
      llvm::FunctionType::get(f64_ty, {f64_ty, f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_pow_f64", module_.get());
  for (const std::string name : {"sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "exp", "log", "log10"}) {
    functions_["math_" + name] = llvm::Function::Create(
        llvm::FunctionType::get(f64_ty, {f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_math_" + name, module_.get());
  }
  functions_["math_atan2"] = llvm::Function::Create(
      llvm::FunctionType::get(f64_ty, {f64_ty, f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_math_atan2", module_.get());
  functions_["matrix_create"] = llvm::Function::Create(
      llvm::FunctionType::get(matrix_ty, {i32_ty, i32_ty}, false), llvm::Function::ExternalLinkage, "fluxion_matrix_create", module_.get());
  functions_["matrix_set"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {matrix_ty, i32_ty, i32_ty, f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_matrix_set", module_.get());
  functions_["matrix_add"] = llvm::Function::Create(
      llvm::FunctionType::get(matrix_ty, {matrix_ty, matrix_ty}, false), llvm::Function::ExternalLinkage, "fluxion_matrix_add", module_.get());
  functions_["matrix_sub"] = llvm::Function::Create(
      llvm::FunctionType::get(matrix_ty, {matrix_ty, matrix_ty}, false), llvm::Function::ExternalLinkage, "fluxion_matrix_sub", module_.get());
  functions_["matrix_mul"] = llvm::Function::Create(
      llvm::FunctionType::get(matrix_ty, {matrix_ty, matrix_ty}, false), llvm::Function::ExternalLinkage, "fluxion_matrix_mul", module_.get());
  functions_["viz_cartpole"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {f64_ty, f64_ty, f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_viz_cartpole", module_.get());
  functions_["viz_cartpole_shove"] = llvm::Function::Create(
      llvm::FunctionType::get(f64_ty, {f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_viz_cartpole_shove", module_.get());
  functions_["otel_span_start"] = llvm::Function::Create(
      llvm::FunctionType::get(i32_ty, {string_ty}, false), llvm::Function::ExternalLinkage, "fluxion_otel_span_start", module_.get());
  functions_["otel_span_end"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {i32_ty}, false), llvm::Function::ExternalLinkage, "fluxion_otel_span_end", module_.get());
  functions_["otel_event_i32"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {string_ty, i32_ty}, false), llvm::Function::ExternalLinkage, "fluxion_otel_event_i32", module_.get());
  functions_["otel_event_f64"] = llvm::Function::Create(
      llvm::FunctionType::get(unit_ty, {string_ty, f64_ty}, false), llvm::Function::ExternalLinkage, "fluxion_otel_event_f64", module_.get());
}

void LlvmCodeGen::declare_records() {
  for (const auto& record : program_.module.records) {
    record_types_[record.name] = llvm::StructType::create(*context_, record.name);
  }
  for (const auto& record : program_.module.records) {
    std::vector<llvm::Type*> fields;
    for (const auto& field : record.fields) {
      fields.push_back(llvm_type(field.type));
    }
    record_types_.at(record.name)->setBody(fields, false);
  }
}

void LlvmCodeGen::declare_functions() {
  for (const auto& fn : program_.module.functions) {
    std::vector<llvm::Type*> params;
    for (const auto& param : fn.params) {
      params.push_back(llvm_type(param.type));
    }
    auto* fn_ty = llvm::FunctionType::get(llvm_type(fn.return_type), params, false);
    auto* llvm_fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, fn.name, module_.get());
    std::size_t i = 0;
    for (auto& arg : llvm_fn->args()) {
      arg.setName(fn.params[i++].name);
    }
    functions_[fn.name] = llvm_fn;
  }
}

void LlvmCodeGen::define_functions() {
  for (const auto& fn : program_.module.functions) {
    auto* llvm_fn = function(fn.name);
    auto* block = llvm::BasicBlock::Create(*context_, "entry", llvm_fn);
    builder_->SetInsertPoint(block);

    std::unordered_map<std::string, Local> locals;
    std::size_t index = 0;
    for (auto& arg : llvm_fn->args()) {
      const auto& param = fn.params[index++];
      auto* alloca = create_entry_alloca(llvm_fn, llvm_type(param.type), param.name);
      builder_->CreateStore(&arg, alloca);
      locals[param.name] = {alloca, param.type};
    }

    llvm::Value* value = codegen_expr(*fn.body, locals);
    builder_->CreateRet(value);
    if (llvm::verifyFunction(*llvm_fn, &llvm::errs())) {
      throw DiagnosticError(fn.loc, "LLVM verification failed for function '" + fn.name + "'");
    }
  }
}

llvm::Type* LlvmCodeGen::llvm_type(const TypeRef& type) {
  switch (type.kind) {
    case TypeKind::I32:
      return llvm::Type::getInt32Ty(*context_);
    case TypeKind::F64:
    case TypeKind::ScalarUnit:
      return llvm::Type::getDoubleTy(*context_);
    case TypeKind::Bool:
      return llvm::Type::getInt1Ty(*context_);
    case TypeKind::String:
      return llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_));
    case TypeKind::Matrix:
      return llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_));
    case TypeKind::Unit:
      return llvm::Type::getInt32Ty(*context_);
    case TypeKind::Record:
      return record_type(type.record_name);
    case TypeKind::Unknown:
      break;
  }
  throw DiagnosticError({"<codegen>", 1, 1}, "unknown type in codegen");
}

llvm::StructType* LlvmCodeGen::record_type(const std::string& name) {
  const auto it = record_types_.find(name);
  if (it == record_types_.end()) {
    throw DiagnosticError({"<codegen>", 1, 1}, "unknown record type '" + name + "'");
  }
  return it->second;
}

llvm::Function* LlvmCodeGen::function(const std::string& name) {
  const auto it = functions_.find(name);
  if (it == functions_.end()) {
    throw DiagnosticError({"<codegen>", 1, 1}, "unknown function '" + name + "'");
  }
  return it->second;
}

llvm::AllocaInst* LlvmCodeGen::create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name) {
  llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmp.CreateAlloca(type, nullptr, name);
}

llvm::Value* LlvmCodeGen::cast_numeric(llvm::Value* value, const TypeRef& from, const TypeRef& to) {
  if (from == to) {
    return value;
  }
  if (from.kind == TypeKind::I32 && (to.kind == TypeKind::F64 || to.kind == TypeKind::ScalarUnit)) {
    return builder_->CreateSIToFP(value, llvm_type(to), "sitofp");
  }
  if (from.kind == TypeKind::ScalarUnit && to.kind == TypeKind::F64) {
    return value;
  }
  throw DiagnosticError({"<codegen>", 1, 1}, "unsupported numeric cast");
}

const RecordDecl& LlvmCodeGen::record_decl(const std::string& name) const {
  for (const auto& record : program_.module.records) {
    if (record.name == name) {
      return record;
    }
  }
  throw DiagnosticError({"<codegen>", 1, 1}, "unknown record '" + name + "'");
}

int LlvmCodeGen::field_index(const std::string& record, const std::string& field, const SourceLocation& loc) const {
  const auto& decl = record_decl(record);
  for (std::size_t i = 0; i < decl.fields.size(); ++i) {
    if (decl.fields[i].name == field) {
      return static_cast<int>(i);
    }
  }
  throw DiagnosticError(loc, "record '" + record + "' has no field '" + field + "'");
}

llvm::Value* LlvmCodeGen::codegen_expr(Expr& expr, std::unordered_map<std::string, Local>& locals) {
  if (auto* e = dynamic_cast<NumberExpr*>(&expr)) {
    if (expr.inferred.kind == TypeKind::I32) {
      return llvm::ConstantInt::get(llvm_type(expr.inferred), std::stoi(e->text), true);
    }
    return llvm::ConstantFP::get(llvm_type(expr.inferred), std::stod(e->text));
  }
  if (auto* e = dynamic_cast<BoolExpr*>(&expr)) {
    return llvm::ConstantInt::get(llvm_type(TypeRef::boolean()), e->value);
  }
  if (auto* e = dynamic_cast<StringExpr*>(&expr)) {
    return builder_->CreateGlobalStringPtr(e->value, "str");
  }
  if (auto* e = dynamic_cast<MatrixExpr*>(&expr)) {
    llvm::Value* rows = llvm::ConstantInt::get(llvm_type(TypeRef::i32()), static_cast<int>(e->rows.size()), true);
    llvm::Value* cols = llvm::ConstantInt::get(llvm_type(TypeRef::i32()), static_cast<int>(e->rows.front().size()), true);
    llvm::Value* matrix = builder_->CreateCall(function("matrix_create"), {rows, cols}, "matrixtmp");
    for (std::size_t r = 0; r < e->rows.size(); ++r) {
      for (std::size_t c = 0; c < e->rows[r].size(); ++c) {
        llvm::Value* value = codegen_expr(*e->rows[r][c], locals);
        value = cast_numeric(value, e->rows[r][c]->inferred, TypeRef::f64());
        builder_->CreateCall(function("matrix_set"),
                             {matrix,
                              llvm::ConstantInt::get(llvm_type(TypeRef::i32()), static_cast<int>(r), true),
                              llvm::ConstantInt::get(llvm_type(TypeRef::i32()), static_cast<int>(c), true),
                              value});
      }
    }
    return matrix;
  }
  if (auto* e = dynamic_cast<VarExpr*>(&expr)) {
    const auto it = locals.find(e->name);
    if (it == locals.end()) {
      if (e->name == "pi" || e->name == "tau" || e->name == "e") {
        return llvm::ConstantFP::get(llvm_type(TypeRef::f64()), builtin_constant_value(e->name));
      }
      throw DiagnosticError(e->loc, "unknown local '" + e->name + "'");
    }
    return builder_->CreateLoad(llvm_type(it->second.type), it->second.storage, e->name);
  }
  if (auto* e = dynamic_cast<AssignExpr*>(&expr)) {
    const auto it = locals.find(e->name);
    if (it == locals.end()) {
      throw DiagnosticError(e->loc, "unknown local '" + e->name + "'");
    }
    llvm::Value* value = codegen_expr(*e->value, locals);
    builder_->CreateStore(value, it->second.storage);
    return llvm::ConstantInt::get(llvm_type(TypeRef::unit()), 0);
  }
  if (auto* e = dynamic_cast<BinaryExpr*>(&expr)) {
    llvm::Value* lhs = codegen_expr(*e->lhs, locals);
    llvm::Value* rhs = codegen_expr(*e->rhs, locals);
    if (expr.inferred.kind == TypeKind::Matrix) {
      if (e->op == "+") return builder_->CreateCall(function("matrix_add"), {lhs, rhs}, "matrixadd");
      if (e->op == "-") return builder_->CreateCall(function("matrix_sub"), {lhs, rhs}, "matrixsub");
      if (e->op == "*") return builder_->CreateCall(function("matrix_mul"), {lhs, rhs}, "matrixmul");
    }
    const bool f64 = e->lhs->inferred.kind == TypeKind::F64 || e->lhs->inferred.kind == TypeKind::ScalarUnit;
    if (e->op == "+") return f64 ? builder_->CreateFAdd(lhs, rhs, "addtmp") : builder_->CreateAdd(lhs, rhs, "addtmp");
    if (e->op == "-") return f64 ? builder_->CreateFSub(lhs, rhs, "subtmp") : builder_->CreateSub(lhs, rhs, "subtmp");
    if (e->op == "*") return f64 ? builder_->CreateFMul(lhs, rhs, "multmp") : builder_->CreateMul(lhs, rhs, "multmp");
    if (e->op == "/") return f64 ? builder_->CreateFDiv(lhs, rhs, "divtmp") : builder_->CreateSDiv(lhs, rhs, "divtmp");
    if (e->op == "^") return builder_->CreateCall(function(f64 ? "pow_f64" : "pow_i32"), {lhs, rhs}, "powtmp");
    if (f64) {
      llvm::CmpInst::Predicate pred = llvm::CmpInst::FCMP_OEQ;
      if (e->op == "<") pred = llvm::CmpInst::FCMP_OLT;
      else if (e->op == "<=") pred = llvm::CmpInst::FCMP_OLE;
      else if (e->op == ">") pred = llvm::CmpInst::FCMP_OGT;
      else if (e->op == ">=") pred = llvm::CmpInst::FCMP_OGE;
      else if (e->op == "!=") pred = llvm::CmpInst::FCMP_ONE;
      return builder_->CreateFCmp(pred, lhs, rhs, "cmptmp");
    }
    llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
    if (e->op == "<") pred = llvm::CmpInst::ICMP_SLT;
    else if (e->op == "<=") pred = llvm::CmpInst::ICMP_SLE;
    else if (e->op == ">") pred = llvm::CmpInst::ICMP_SGT;
    else if (e->op == ">=") pred = llvm::CmpInst::ICMP_SGE;
    else if (e->op == "!=") pred = llvm::CmpInst::ICMP_NE;
    return builder_->CreateICmp(pred, lhs, rhs, "cmptmp");
  }
  if (auto* e = dynamic_cast<CallExpr*>(&expr)) {
    std::vector<llvm::Value*> args;
    for (auto& arg : e->args) {
      args.push_back(codegen_expr(*arg, locals));
    }
    if (is_math_f64_unary(e->callee)) {
      return builder_->CreateCall(function("math_" + e->callee), args, e->callee + "tmp");
    }
    if (e->callee == "atan2") {
      return builder_->CreateCall(function("math_atan2"), args, "atan2tmp");
    }
    if (e->callee == "abs") {
      const bool f64 = expr.inferred.kind != TypeKind::I32;
      llvm::Value* zero = f64
                              ? static_cast<llvm::Value*>(llvm::ConstantFP::get(llvm_type(TypeRef::f64()), 0.0))
                              : static_cast<llvm::Value*>(llvm::ConstantInt::get(llvm_type(TypeRef::i32()), 0, true));
      llvm::Value* is_negative = f64
                                     ? builder_->CreateFCmpOLT(args[0], zero, "absneg")
                                     : builder_->CreateICmpSLT(args[0], zero, "absneg");
      llvm::Value* negated = f64 ? builder_->CreateFNeg(args[0], "absval") : builder_->CreateNeg(args[0], "absval");
      return builder_->CreateSelect(is_negative, negated, args[0], "abstmp");
    }
    if (e->callee == "min" || e->callee == "max") {
      const bool f64 = expr.inferred.kind != TypeKind::I32;
      llvm::Value* choose_lhs = nullptr;
      if (e->callee == "min") {
        choose_lhs = f64 ? builder_->CreateFCmpOLT(args[0], args[1], "mintest")
                         : builder_->CreateICmpSLT(args[0], args[1], "mintest");
      } else {
        choose_lhs = f64 ? builder_->CreateFCmpOGT(args[0], args[1], "maxtest")
                         : builder_->CreateICmpSGT(args[0], args[1], "maxtest");
      }
      return builder_->CreateSelect(choose_lhs, args[0], args[1], e->callee + "tmp");
    }
    if (e->callee == "clamp") {
      const bool f64 = expr.inferred.kind != TypeKind::I32;
      llvm::Value* below = f64 ? builder_->CreateFCmpOLT(args[0], args[1], "clamplo")
                               : builder_->CreateICmpSLT(args[0], args[1], "clamplo");
      llvm::Value* above = f64 ? builder_->CreateFCmpOGT(args[0], args[2], "clamphi")
                               : builder_->CreateICmpSGT(args[0], args[2], "clamphi");
      llvm::Value* lo_or_value = builder_->CreateSelect(below, args[1], args[0], "clampmintmp");
      return builder_->CreateSelect(above, args[2], lo_or_value, "clamptmp");
    }
    return builder_->CreateCall(function(e->callee), args, expr.inferred.kind == TypeKind::Unit ? "unittmp" : "calltmp");
  }
  if (auto* e = dynamic_cast<RecordLiteralExpr*>(&expr)) {
    llvm::Value* value = llvm::UndefValue::get(record_type(e->type_name));
    for (const auto& item : e->fields) {
      const int index = field_index(e->type_name, item.first, e->loc);
      value = builder_->CreateInsertValue(value, codegen_expr(*item.second, locals), {static_cast<unsigned>(index)}, "recordtmp");
    }
    return value;
  }
  if (auto* e = dynamic_cast<FieldExpr*>(&expr)) {
    llvm::Value* object = codegen_expr(*e->object, locals);
    const int index = field_index(e->object->inferred.record_name, e->field, e->loc);
    return builder_->CreateExtractValue(object, {static_cast<unsigned>(index)}, "fieldtmp");
  }
  if (auto* e = dynamic_cast<IfExpr*>(&expr)) {
    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(*context_, "then", fn);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(*context_, "else");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(*context_, "ifcont");
    builder_->CreateCondBr(codegen_expr(*e->condition, locals), then_bb, else_bb);
    builder_->SetInsertPoint(then_bb);
    llvm::Value* then_value = codegen_expr(*e->then_expr, locals);
    builder_->CreateBr(merge_bb);
    then_bb = builder_->GetInsertBlock();
    fn->insert(fn->end(), else_bb);
    builder_->SetInsertPoint(else_bb);
    llvm::Value* else_value = codegen_expr(*e->else_expr, locals);
    builder_->CreateBr(merge_bb);
    else_bb = builder_->GetInsertBlock();
    fn->insert(fn->end(), merge_bb);
    builder_->SetInsertPoint(merge_bb);
    if (expr.inferred.kind == TypeKind::Unit) {
      return llvm::ConstantInt::get(llvm_type(TypeRef::unit()), 0);
    }
    llvm::PHINode* phi = builder_->CreatePHI(llvm_type(expr.inferred), 2, "iftmp");
    phi->addIncoming(then_value, then_bb);
    phi->addIncoming(else_value, else_bb);
    return phi;
  }
  if (auto* e = dynamic_cast<LetExpr*>(&expr)) {
    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::Value* value = codegen_expr(*e->value, locals);
    auto* alloca = create_entry_alloca(fn, llvm_type(e->value->inferred), e->name);
    builder_->CreateStore(value, alloca);
    auto nested = locals;
    nested[e->name] = {alloca, e->value->inferred};
    return codegen_expr(*e->body, nested);
  }
  if (auto* e = dynamic_cast<SequenceExpr*>(&expr)) {
    llvm::Value* last = llvm::ConstantInt::get(llvm_type(TypeRef::unit()), 0);
    for (auto& item : e->expressions) {
      last = codegen_expr(*item, locals);
    }
    return last;
  }
  if (auto* e = dynamic_cast<ForExpr*>(&expr)) {
    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::Value* start = codegen_expr(*e->start, locals);
    llvm::Value* end = codegen_expr(*e->end, locals);
    auto* index_alloca = create_entry_alloca(fn, llvm_type(TypeRef::i32()), e->var);
    builder_->CreateStore(start, index_alloca);
    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(*context_, "for.cond", fn);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(*context_, "for.body");
    llvm::BasicBlock* done_bb = llvm::BasicBlock::Create(*context_, "for.done");
    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    llvm::Value* index = builder_->CreateLoad(llvm_type(TypeRef::i32()), index_alloca, e->var);
    llvm::Value* cond = builder_->CreateICmpSLT(index, end, "forcond");
    builder_->CreateCondBr(cond, body_bb, done_bb);
    fn->insert(fn->end(), body_bb);
    builder_->SetInsertPoint(body_bb);
    auto nested = locals;
    nested[e->var] = {index_alloca, TypeRef::i32()};
    codegen_expr(*e->body, nested);
    llvm::Value* next = builder_->CreateAdd(builder_->CreateLoad(llvm_type(TypeRef::i32()), index_alloca), llvm::ConstantInt::get(llvm_type(TypeRef::i32()), 1), "next");
    builder_->CreateStore(next, index_alloca);
    builder_->CreateBr(cond_bb);
    fn->insert(fn->end(), done_bb);
    builder_->SetInsertPoint(done_bb);
    return codegen_expr(*e->result, locals);
  }
  throw DiagnosticError(expr.loc, "internal codegen error");
}

}  // namespace fluxion
