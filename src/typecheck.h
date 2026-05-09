#pragma once

#include "ast.h"

#include <unordered_map>

namespace fluxion {

struct CheckedProgram {
  Module module;
};

class TypeChecker {
 public:
  explicit TypeChecker(Module module);
  CheckedProgram check();

 private:
  struct FunctionSig {
    std::vector<TypeRef> params;
    TypeRef result;
  };

  TypeRef check_expr(Expr& expr, std::unordered_map<std::string, TypeRef>& locals);
  TypeRef require_numeric(BinaryExpr& expr, std::unordered_map<std::string, TypeRef>& locals);
  TypeRef resolve_type(const TypeRef& type, const SourceLocation& loc) const;
  bool compatible(const TypeRef& actual, const TypeRef& expected) const;
  TypeRef matrix_binary_type(const TypeRef& lhs, const TypeRef& rhs, const std::string& op, const SourceLocation& loc) const;
  const RecordDecl& record(const std::string& name, const SourceLocation& loc) const;
  const Parameter& field(const RecordDecl& decl, const std::string& name, const SourceLocation& loc) const;
  void expect(const TypeRef& actual, const TypeRef& expected, const SourceLocation& loc);

  Module module_;
  std::unordered_map<std::string, TypeRef> aliases_;
  std::unordered_map<std::string, RecordDecl*> records_;
  std::unordered_map<std::string, FunctionSig> functions_;
};

}  // namespace fluxion
