#pragma once

#include "typecheck.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace fluxion {

enum class PhysicsSeverity { Ok, Info, Warning };

struct PhysicsDiagnostic {
  PhysicsSeverity severity = PhysicsSeverity::Info;
  std::string code;
  SourceLocation loc;
  std::string message;
  std::vector<std::string> notes;
};

class PhysicsChecker {
 public:
  explicit PhysicsChecker(const CheckedProgram& program);
  std::vector<PhysicsDiagnostic> check() const;

 private:
  void check_aliases(std::vector<PhysicsDiagnostic>& diagnostics) const;
  void check_records(std::vector<PhysicsDiagnostic>& diagnostics) const;
  void check_function(const FunctionDecl& fn, std::vector<PhysicsDiagnostic>& diagnostics) const;
  void walk_expr(const Expr& expr, const FunctionDecl& fn, std::vector<PhysicsDiagnostic>& diagnostics) const;
  bool contains_call(const Expr& expr, const std::string& name_fragment) const;
  bool contains_var_or_call(const Expr& expr, const std::string& fragment) const;
  bool looks_like_simple_covariance_update(const Expr& expr) const;
  bool is_square_matrix(const TypeRef& type) const;
  bool is_dimensioned_matrix(const TypeRef& type) const;

  const CheckedProgram& program_;
};

void print_physics_diagnostics(const std::vector<PhysicsDiagnostic>& diagnostics, std::ostream& out);

}  // namespace fluxion
