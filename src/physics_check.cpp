#include "physics_check.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace fluxion {
namespace {

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool contains_text(const std::string& text, const std::string& fragment) {
  return lower(text).find(lower(fragment)) != std::string::npos;
}

const char* severity_name(PhysicsSeverity severity) {
  switch (severity) {
    case PhysicsSeverity::Ok:
      return "ok";
    case PhysicsSeverity::Info:
      return "info";
    case PhysicsSeverity::Warning:
      return "warn";
  }
  return "info";
}

std::string matrix_shape(const TypeRef& type) {
  std::ostringstream out;
  out << type.matrix_rows << "x" << type.matrix_cols;
  return out.str();
}

}  // namespace

PhysicsChecker::PhysicsChecker(const CheckedProgram& program) : program_(program) {}

std::vector<PhysicsDiagnostic> PhysicsChecker::check() const {
  std::vector<PhysicsDiagnostic> diagnostics;
  check_aliases(diagnostics);
  check_records(diagnostics);
  for (const auto& fn : program_.module.functions) {
    check_function(fn, diagnostics);
  }
  if (diagnostics.empty()) {
    diagnostics.push_back({PhysicsSeverity::Info,
                           "FXN-PHYS-000",
                           {"<module>", 1, 1},
                           "no physics-specific facts were found to analyze",
                           {}});
  }
  return diagnostics;
}

void PhysicsChecker::check_aliases(std::vector<PhysicsDiagnostic>& diagnostics) const {
  int dimensioned = 0;
  int covariance_like = 0;
  for (const auto& alias : program_.module.aliases) {
    if (!is_dimensioned_matrix(alias.target)) {
      continue;
    }
    ++dimensioned;
    if (is_square_matrix(alias.target) && contains_text(alias.name, "cov")) {
      ++covariance_like;
      diagnostics.push_back({PhysicsSeverity::Info,
                             "FXN-COV-001",
                             alias.loc,
                             "covariance-like alias '" + alias.name + "' is a square " + matrix_shape(alias.target) + " matrix",
                             {"physics-check will apply covariance-specific lint rules to functions that update this shape"}});
    }
  }
  if (dimensioned > 0) {
    diagnostics.push_back({PhysicsSeverity::Ok,
                           "FXN-MAT-001",
                           program_.module.aliases.front().loc,
                           "module declares " + std::to_string(dimensioned) + " dimensioned matrix alias" +
                               (dimensioned == 1 ? "" : "es"),
                           {covariance_like == 0 ? "no covariance-like square matrix aliases were named"
                                                 : std::to_string(covariance_like) + " covariance-like alias" +
                                                       (covariance_like == 1 ? " was" : "es were") + " recognized"}});
  }
}

void PhysicsChecker::check_records(std::vector<PhysicsDiagnostic>& diagnostics) const {
  for (const auto& record : program_.module.records) {
    int scalar_units = 0;
    for (const auto& field : record.fields) {
      if (field.type.kind == TypeKind::ScalarUnit) {
        ++scalar_units;
      }
    }
    if (scalar_units == 0) {
      continue;
    }
    diagnostics.push_back({PhysicsSeverity::Ok,
                           "FXN-UNIT-001",
                           record.loc,
                           "record '" + record.name + "' carries " + std::to_string(scalar_units) +
                               " physically typed scalar field" + (scalar_units == 1 ? "" : "s"),
                           {"nominal scalar-unit aliases prevent accidental mixing of fields such as meters and meters/second"}});
  }
}

void PhysicsChecker::check_function(const FunctionDecl& fn, std::vector<PhysicsDiagnostic>& diagnostics) const {
  if (fn.body) {
    walk_expr(*fn.body, fn, diagnostics);
  }
}

void PhysicsChecker::walk_expr(const Expr& expr, const FunctionDecl& fn, std::vector<PhysicsDiagnostic>& diagnostics) const {
  if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
    if (contains_text(call->callee, "inverse")) {
      diagnostics.push_back({PhysicsSeverity::Warning,
                             "FXN-NUM-021",
                             call->loc,
                             "matrix inverse is used in '" + fn.name + "'",
                             {"prefer solving the linear system directly; use an SPD or Cholesky solve when the innovation covariance is symmetric positive definite"}});
    }
    for (const auto& arg : call->args) {
      walk_expr(*arg, fn, diagnostics);
    }
    return;
  }

  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
    if ((binary->op == ">" || binary->op == ">=") && contains_var_or_call(*binary->lhs, "innovation")) {
      diagnostics.push_back({PhysicsSeverity::Warning,
                             "FXN-STOCH-014",
                             binary->loc,
                             "innovation gate compares a residual to a raw scalar threshold in '" + fn.name + "'",
                             {"residual vectors often mix measurement units; prefer Mahalanobis gating with d2 = innovation^T S^-1 innovation"}});
    }
    if (looks_like_simple_covariance_update(expr)) {
      diagnostics.push_back({PhysicsSeverity::Warning,
                             "FXN-COV-003",
                             binary->loc,
                             "covariance update in '" + fn.name + "' may not preserve symmetry or positive semidefiniteness",
                             {"simple form detected: (I - K H) * P", "consider Joseph form: P = (I - K H) P (I - K H)^T + K R K^T"}});
    }
    walk_expr(*binary->lhs, fn, diagnostics);
    walk_expr(*binary->rhs, fn, diagnostics);
    return;
  }

  if (const auto* matrix = dynamic_cast<const MatrixExpr*>(&expr)) {
    for (const auto& row : matrix->rows) {
      for (const auto& item : row) {
        walk_expr(*item, fn, diagnostics);
      }
    }
    return;
  }
  if (const auto* field = dynamic_cast<const FieldExpr*>(&expr)) {
    walk_expr(*field->object, fn, diagnostics);
    return;
  }
  if (const auto* record = dynamic_cast<const RecordLiteralExpr*>(&expr)) {
    for (const auto& item : record->fields) {
      walk_expr(*item.second, fn, diagnostics);
    }
    return;
  }
  if (const auto* if_expr = dynamic_cast<const IfExpr*>(&expr)) {
    walk_expr(*if_expr->condition, fn, diagnostics);
    walk_expr(*if_expr->then_expr, fn, diagnostics);
    walk_expr(*if_expr->else_expr, fn, diagnostics);
    return;
  }
  if (const auto* let = dynamic_cast<const LetExpr*>(&expr)) {
    walk_expr(*let->value, fn, diagnostics);
    walk_expr(*let->body, fn, diagnostics);
    return;
  }
  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
    walk_expr(*assign->value, fn, diagnostics);
    return;
  }
  if (const auto* seq = dynamic_cast<const SequenceExpr*>(&expr)) {
    for (const auto& item : seq->expressions) {
      walk_expr(*item, fn, diagnostics);
    }
    return;
  }
  if (const auto* loop = dynamic_cast<const ForExpr*>(&expr)) {
    walk_expr(*loop->start, fn, diagnostics);
    walk_expr(*loop->end, fn, diagnostics);
    walk_expr(*loop->body, fn, diagnostics);
    walk_expr(*loop->result, fn, diagnostics);
  }
}

bool PhysicsChecker::contains_call(const Expr& expr, const std::string& name_fragment) const {
  if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
    if (contains_text(call->callee, name_fragment)) {
      return true;
    }
    for (const auto& arg : call->args) {
      if (contains_call(*arg, name_fragment)) {
        return true;
      }
    }
    return false;
  }
  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
    return contains_call(*binary->lhs, name_fragment) || contains_call(*binary->rhs, name_fragment);
  }
  if (const auto* field = dynamic_cast<const FieldExpr*>(&expr)) {
    return contains_call(*field->object, name_fragment);
  }
  if (const auto* let = dynamic_cast<const LetExpr*>(&expr)) {
    return contains_call(*let->value, name_fragment) || contains_call(*let->body, name_fragment);
  }
  if (const auto* seq = dynamic_cast<const SequenceExpr*>(&expr)) {
    for (const auto& item : seq->expressions) {
      if (contains_call(*item, name_fragment)) {
        return true;
      }
    }
  }
  return false;
}

bool PhysicsChecker::contains_var_or_call(const Expr& expr, const std::string& fragment) const {
  if (const auto* var = dynamic_cast<const VarExpr*>(&expr)) {
    return contains_text(var->name, fragment);
  }
  if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
    if (contains_text(call->callee, fragment)) {
      return true;
    }
    for (const auto& arg : call->args) {
      if (contains_var_or_call(*arg, fragment)) {
        return true;
      }
    }
    return false;
  }
  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
    return contains_var_or_call(*binary->lhs, fragment) || contains_var_or_call(*binary->rhs, fragment);
  }
  if (const auto* field = dynamic_cast<const FieldExpr*>(&expr)) {
    return contains_text(field->field, fragment) || contains_var_or_call(*field->object, fragment);
  }
  return false;
}

bool PhysicsChecker::looks_like_simple_covariance_update(const Expr& expr) const {
  const auto* multiply = dynamic_cast<const BinaryExpr*>(&expr);
  if (multiply == nullptr || multiply->op != "*" || !is_square_matrix(expr.inferred)) {
    return false;
  }
  const auto* lhs = dynamic_cast<const BinaryExpr*>(multiply->lhs.get());
  if (lhs == nullptr || lhs->op != "-") {
    return false;
  }
  return contains_call(*lhs->lhs, "identity") && contains_call(*lhs->rhs, "gain") &&
         contains_call(*lhs->rhs, "observation");
}

bool PhysicsChecker::is_square_matrix(const TypeRef& type) const {
  return type.kind == TypeKind::Matrix && type.matrix_rows > 0 && type.matrix_rows == type.matrix_cols;
}

bool PhysicsChecker::is_dimensioned_matrix(const TypeRef& type) const {
  return type.kind == TypeKind::Matrix && type.matrix_rows > 0 && type.matrix_cols > 0;
}

void print_physics_diagnostics(const std::vector<PhysicsDiagnostic>& diagnostics, std::ostream& out) {
  for (const auto& diagnostic : diagnostics) {
    out << "[" << severity_name(diagnostic.severity) << "] " << diagnostic.code << "\n"
        << "  " << diagnostic.message << "\n";
    if (!diagnostic.loc.file.empty() && diagnostic.loc.file != "<module>") {
      out << "  at " << diagnostic.loc.file << ":" << diagnostic.loc.line << ":" << diagnostic.loc.column << "\n";
    }
    for (const auto& note : diagnostic.notes) {
      out << "  note: " << note << "\n";
    }
    out << "\n";
  }
}

}  // namespace fluxion
