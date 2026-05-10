#include "typecheck.h"

#include <unordered_set>

namespace fluxion {

namespace {

bool is_math_f64_unary(const std::string& name) {
  return name == "sin" || name == "cos" || name == "tan" || name == "asin" || name == "acos" ||
         name == "atan" || name == "sqrt" || name == "exp" || name == "log" || name == "log10";
}

bool is_builtin_constant(const std::string& name) {
  return name == "pi" || name == "tau" || name == "e";
}

bool is_matching_numeric(const TypeRef& lhs, const TypeRef& rhs) {
  return lhs == rhs && lhs.is_numeric();
}

}  // namespace

TypeChecker::TypeChecker(Module module) : module_(std::move(module)) {}

CheckedProgram TypeChecker::check() {
  for (const auto& alias : module_.aliases) {
    if (aliases_.count(alias.name)) {
      throw DiagnosticError(alias.loc, "duplicate type alias '" + alias.name + "'");
    }
    aliases_[alias.name] = alias.target;
  }
  for (auto& record : module_.records) {
    if (records_.count(record.name)) {
      throw DiagnosticError(record.loc, "duplicate record '" + record.name + "'");
    }
    for (auto& field : record.fields) {
      field.type = resolve_type(field.type, field.loc);
    }
    records_[record.name] = &record;
  }
  for (auto& fn : module_.functions) {
    if (functions_.count(fn.name)) {
      throw DiagnosticError(fn.loc, "duplicate function '" + fn.name + "'");
    }
    FunctionSig sig;
    for (auto& param : fn.params) {
      param.type = resolve_type(param.type, param.loc);
      sig.params.push_back(param.type);
    }
    fn.return_type = resolve_type(fn.return_type, fn.loc);
    sig.result = fn.return_type;
    functions_[fn.name] = sig;
  }
  for (auto& fn : module_.functions) {
    std::unordered_map<std::string, TypeRef> locals;
    for (const auto& param : fn.params) {
      locals[param.name] = param.type;
    }
    const TypeRef body = check_expr(*fn.body, locals);
    expect(body, fn.return_type, fn.loc);
  }
  check_phases();
  check_reactors();
  if (module_.reactors.empty() && !functions_.count("main")) {
    throw DiagnosticError({"<module>", 1, 1}, "missing func main() -> Int");
  }
  if (functions_.count("main")) {
    const auto& main = functions_.at("main");
    if (!main.params.empty() || main.result != TypeRef::i32()) {
      throw DiagnosticError({"<module>", 1, 1}, "main must have type func main() -> Int");
    }
  }
  return {std::move(module_)};
}

void TypeChecker::check_phases() {
  std::unordered_set<std::string> names;
  for (const auto& phase : module_.phases) {
    if (!names.insert(phase.name).second) {
      throw DiagnosticError(phase.loc, "duplicate phase '" + phase.name + "'");
    }
  }
  for (const auto& phase : module_.phases) {
    if (!phase.related_phase.empty() && !names.count(phase.related_phase)) {
      throw DiagnosticError(phase.loc, "phase '" + phase.name + "' references unknown phase '" + phase.related_phase + "'");
    }
  }
}

void TypeChecker::check_reactors() {
  std::unordered_set<std::string> phase_names;
  for (const auto& phase : module_.phases) {
    phase_names.insert(phase.name);
  }
  std::unordered_set<std::string> reactor_names;
  for (const auto& reactor : module_.reactors) {
    if (!reactor_names.insert(reactor.name).second) {
      throw DiagnosticError(reactor.loc, "duplicate reactor '" + reactor.name + "'");
    }
    if (!reactor.meta.phase.empty() && !phase_names.empty() && !phase_names.count(reactor.meta.phase)) {
      throw DiagnosticError(reactor.loc, "reactor '" + reactor.name + "' uses unknown phase '" + reactor.meta.phase + "'");
    }
    if (reactor.meta.tick.empty()) {
      throw DiagnosticError(reactor.loc, "reactor '" + reactor.name + "' must declare a tick rate");
    }
    std::unordered_set<std::string> names;
    for (const auto& port : reactor.ports) {
      if (!names.insert(port.name).second) {
        throw DiagnosticError(port.loc, "duplicate reactor member '" + port.name + "'");
      }
      if (port.type.text.empty()) {
        throw DiagnosticError(port.loc, "reactor port '" + port.name + "' must declare a payload type");
      }
      if (port.kind == ReactorPortDecl::Kind::Stream) {
        if (port.capacity <= 0) {
          throw DiagnosticError(port.loc, "stream port '" + port.name + "' must declare positive capacity");
        }
        if (port.overflow.empty()) {
          throw DiagnosticError(port.loc, "stream port '" + port.name + "' must declare overflow policy");
        }
      }
      if (!port.overflow.empty() && port.overflow != "drop_oldest" && port.overflow != "drop_newest" &&
          port.overflow != "coalesce" && port.overflow != "fault") {
        throw DiagnosticError(port.loc, "unknown overflow policy '" + port.overflow + "'");
      }
    }
    for (const auto& state : reactor.states) {
      if (!names.insert(state.name).second) {
        throw DiagnosticError(state.loc, "duplicate reactor member '" + state.name + "'");
      }
      if (state.region.empty() && state.initializer.empty()) {
        throw DiagnosticError(state.loc, "reactor state '" + state.name + "' must declare a region");
      }
    }
    if (reactor.handlers.empty()) {
      throw DiagnosticError(reactor.loc, "reactor '" + reactor.name + "' must declare at least one handler");
    }
  }
}

const RecordDecl& TypeChecker::record(const std::string& name, const SourceLocation& loc) const {
  const auto it = records_.find(name);
  if (it == records_.end()) {
    throw DiagnosticError(loc, "unknown record type '" + name + "'");
  }
  return *it->second;
}

const Parameter& TypeChecker::field(const RecordDecl& decl, const std::string& name, const SourceLocation& loc) const {
  for (const auto& field : decl.fields) {
    if (field.name == name) {
      return field;
    }
  }
  throw DiagnosticError(loc, "record '" + decl.name + "' has no field '" + name + "'");
}

TypeRef TypeChecker::resolve_type(const TypeRef& type, const SourceLocation& loc) const {
  if (type.kind != TypeKind::Record) {
    return type;
  }
  const auto it = aliases_.find(type.record_name);
  if (it == aliases_.end()) {
    return type;
  }
  TypeRef resolved = resolve_type(it->second, loc);
  if (resolved.kind == TypeKind::F64) {
    return TypeRef::scalar_unit(type.record_name);
  }
  return resolved;
}

bool TypeChecker::compatible(const TypeRef& actual, const TypeRef& expected) const {
  if (actual.kind == TypeKind::F64 && expected.kind == TypeKind::ScalarUnit) {
    return true;
  }
  if (actual.kind != expected.kind) {
    return false;
  }
  if (actual.kind == TypeKind::Matrix) {
    const bool rows_match = expected.matrix_rows < 0 || actual.matrix_rows < 0 || actual.matrix_rows == expected.matrix_rows;
    const bool cols_match = expected.matrix_cols < 0 || actual.matrix_cols < 0 || actual.matrix_cols == expected.matrix_cols;
    return rows_match && cols_match;
  }
  return actual == expected;
}

void TypeChecker::expect(const TypeRef& actual, const TypeRef& expected, const SourceLocation& loc) {
  if (!compatible(actual, expected)) {
    throw DiagnosticError(loc, "expected " + expected.str() + " but found " + actual.str());
  }
}

TypeRef TypeChecker::require_numeric(BinaryExpr& expr, std::unordered_map<std::string, TypeRef>& locals) {
  const TypeRef lhs = check_expr(*expr.lhs, locals);
  const TypeRef rhs = check_expr(*expr.rhs, locals);
  if (lhs != rhs || !lhs.is_numeric()) {
    throw DiagnosticError(expr.loc, "numeric operator requires matching i32 or f64 operands");
  }
  return lhs;
}

TypeRef TypeChecker::matrix_binary_type(const TypeRef& lhs,
                                        const TypeRef& rhs,
                                        const std::string& op,
                                        const SourceLocation& loc) const {
  if (lhs.kind != TypeKind::Matrix || rhs.kind != TypeKind::Matrix || (op != "+" && op != "-" && op != "*")) {
    throw DiagnosticError(loc, "matrix operators require Matrix operands and support '+', '-', and '*'");
  }
  if (op == "+" || op == "-") {
    if (!compatible(lhs, rhs) || !compatible(rhs, lhs)) {
      throw DiagnosticError(loc, "matrix '" + op + "' requires matching dimensions but found " + lhs.str() + " and " + rhs.str());
    }
    return lhs.matrix_rows >= 0 && lhs.matrix_cols >= 0 ? lhs : rhs;
  }
  if (lhs.matrix_cols >= 0 && rhs.matrix_rows >= 0 && lhs.matrix_cols != rhs.matrix_rows) {
    throw DiagnosticError(loc, "matrix '*' requires lhs columns to equal rhs rows but found " + lhs.str() + " and " + rhs.str());
  }
  if (lhs.matrix_rows >= 0 && rhs.matrix_cols >= 0) {
    return TypeRef::matrix(lhs.matrix_rows, rhs.matrix_cols);
  }
  return TypeRef::matrix();
}

TypeRef TypeChecker::check_expr(Expr& expr, std::unordered_map<std::string, TypeRef>& locals) {
  if (auto* e = dynamic_cast<NumberExpr*>(&expr)) {
    expr.inferred = e->integer ? TypeRef::i32() : TypeRef::f64();
  } else if (dynamic_cast<StringExpr*>(&expr)) {
    expr.inferred = TypeRef::string();
  } else if (auto* e = dynamic_cast<MatrixExpr*>(&expr)) {
    if (e->rows.empty()) {
      throw DiagnosticError(e->loc, "matrix literal must have at least one row");
    }
    const auto cols = e->rows.front().size();
    if (cols == 0) {
      throw DiagnosticError(e->loc, "matrix literal rows must have at least one element");
    }
    for (auto& row : e->rows) {
      if (row.size() != cols) {
        throw DiagnosticError(e->loc, "matrix literal rows must have the same length");
      }
      for (auto& item : row) {
        const TypeRef item_type = check_expr(*item, locals);
        if (item_type.kind != TypeKind::I32 && item_type.kind != TypeKind::F64 && item_type.kind != TypeKind::ScalarUnit) {
          throw DiagnosticError(item->loc, "matrix elements must be Int or Double");
        }
      }
    }
    expr.inferred = TypeRef::matrix(static_cast<int>(e->rows.size()), static_cast<int>(cols));
  } else if (dynamic_cast<BoolExpr*>(&expr)) {
    expr.inferred = TypeRef::boolean();
  } else if (auto* e = dynamic_cast<VarExpr*>(&expr)) {
    const auto it = locals.find(e->name);
    if (it == locals.end()) {
      if (is_builtin_constant(e->name)) {
        expr.inferred = TypeRef::f64();
      } else {
        throw DiagnosticError(e->loc, "unknown variable '" + e->name + "'");
      }
    } else {
      expr.inferred = it->second;
    }
  } else if (auto* e = dynamic_cast<BinaryExpr*>(&expr)) {
    const TypeRef lhs = check_expr(*e->lhs, locals);
    const TypeRef rhs = check_expr(*e->rhs, locals);
    if (lhs.kind == TypeKind::Matrix || rhs.kind == TypeKind::Matrix) {
      expr.inferred = matrix_binary_type(lhs, rhs, e->op, e->loc);
    } else if (e->op == "<" || e->op == "<=" || e->op == ">" || e->op == ">=" || e->op == "==" || e->op == "!=") {
      require_numeric(*e, locals);
      expr.inferred = TypeRef::boolean();
    } else {
      if (lhs != rhs || !lhs.is_numeric()) {
        throw DiagnosticError(e->loc, "numeric operator requires matching Int or Double operands");
      }
      expr.inferred = lhs;
    }
  } else if (auto* e = dynamic_cast<CallExpr*>(&expr)) {
    if (is_math_f64_unary(e->callee)) {
      if (e->args.size() != 1) {
        throw DiagnosticError(e->loc, "wrong argument count for '" + e->callee + "'");
      }
      expect(check_expr(*e->args[0], locals), TypeRef::f64(), e->args[0]->loc);
      expr.inferred = TypeRef::f64();
      return expr.inferred;
    }
    if (e->callee == "atan2") {
      if (e->args.size() != 2) {
        throw DiagnosticError(e->loc, "wrong argument count for 'atan2'");
      }
      expect(check_expr(*e->args[0], locals), TypeRef::f64(), e->args[0]->loc);
      expect(check_expr(*e->args[1], locals), TypeRef::f64(), e->args[1]->loc);
      expr.inferred = TypeRef::f64();
      return expr.inferred;
    }
    if (e->callee == "abs") {
      if (e->args.size() != 1) {
        throw DiagnosticError(e->loc, "wrong argument count for 'abs'");
      }
      const TypeRef arg = check_expr(*e->args[0], locals);
      if (!arg.is_numeric()) {
        throw DiagnosticError(e->args[0]->loc, "abs requires an Int or Double argument");
      }
      expr.inferred = arg;
      return expr.inferred;
    }
    if (e->callee == "min" || e->callee == "max") {
      if (e->args.size() != 2) {
        throw DiagnosticError(e->loc, "wrong argument count for '" + e->callee + "'");
      }
      const TypeRef lhs = check_expr(*e->args[0], locals);
      const TypeRef rhs = check_expr(*e->args[1], locals);
      if (!is_matching_numeric(lhs, rhs)) {
        throw DiagnosticError(e->loc, e->callee + " requires matching Int or Double arguments");
      }
      expr.inferred = lhs;
      return expr.inferred;
    }
    if (e->callee == "clamp") {
      if (e->args.size() != 3) {
        throw DiagnosticError(e->loc, "wrong argument count for 'clamp'");
      }
      const TypeRef value = check_expr(*e->args[0], locals);
      const TypeRef lo = check_expr(*e->args[1], locals);
      const TypeRef hi = check_expr(*e->args[2], locals);
      if (!is_matching_numeric(value, lo) || !is_matching_numeric(value, hi)) {
        throw DiagnosticError(e->loc, "clamp requires matching Int or Double arguments");
      }
      expr.inferred = value;
      return expr.inferred;
    }
    const auto it = functions_.find(e->callee);
    if (it == functions_.end()) {
      if (e->callee == "print_f64") {
        functions_[e->callee] = {{TypeRef::f64()}, TypeRef::unit()};
      } else if (e->callee == "print_i32") {
        functions_[e->callee] = {{TypeRef::i32()}, TypeRef::unit()};
      } else if (e->callee == "print_bool") {
        functions_[e->callee] = {{TypeRef::boolean()}, TypeRef::unit()};
      } else if (e->callee == "print_string") {
        functions_[e->callee] = {{TypeRef::string()}, TypeRef::unit()};
      } else if (e->callee == "print_matrix") {
        functions_[e->callee] = {{TypeRef::matrix()}, TypeRef::unit()};
      } else if (e->callee == "viz_cartpole") {
        functions_[e->callee] = {{TypeRef::f64(), TypeRef::f64(), TypeRef::f64()}, TypeRef::unit()};
      } else if (e->callee == "viz_cartpole_shove") {
        functions_[e->callee] = {{TypeRef::f64()}, TypeRef::f64()};
      } else if (e->callee == "otel_span_start") {
        functions_[e->callee] = {{TypeRef::string()}, TypeRef::i32()};
      } else if (e->callee == "otel_span_end") {
        functions_[e->callee] = {{TypeRef::i32()}, TypeRef::unit()};
      } else if (e->callee == "otel_event_i32") {
        functions_[e->callee] = {{TypeRef::string(), TypeRef::i32()}, TypeRef::unit()};
      } else if (e->callee == "otel_event_f64") {
        functions_[e->callee] = {{TypeRef::string(), TypeRef::f64()}, TypeRef::unit()};
      } else {
        throw DiagnosticError(e->loc, "unknown function '" + e->callee + "'");
      }
    }
    const auto& sig = functions_.at(e->callee);
    if (sig.params.size() != e->args.size()) {
      throw DiagnosticError(e->loc, "wrong argument count for '" + e->callee + "'");
    }
    for (std::size_t i = 0; i < e->args.size(); ++i) {
      expect(check_expr(*e->args[i], locals), sig.params[i], e->args[i]->loc);
    }
    expr.inferred = sig.result;
  } else if (auto* e = dynamic_cast<FieldExpr*>(&expr)) {
    const TypeRef object = check_expr(*e->object, locals);
    if (object.kind != TypeKind::Record) {
      throw DiagnosticError(e->loc, "field access requires record value");
    }
    expr.inferred = field(record(object.record_name, e->loc), e->field, e->loc).type;
  } else if (auto* e = dynamic_cast<RecordLiteralExpr*>(&expr)) {
    const auto& decl = record(e->type_name, e->loc);
    for (const auto& f : e->fields) {
      const auto& expected = field(decl, f.first, e->loc);
      expect(check_expr(*f.second, locals), expected.type, f.second->loc);
    }
    expr.inferred = TypeRef::record(e->type_name);
  } else if (auto* e = dynamic_cast<IfExpr*>(&expr)) {
    expect(check_expr(*e->condition, locals), TypeRef::boolean(), e->condition->loc);
    const TypeRef then_type = check_expr(*e->then_expr, locals);
    const TypeRef else_type = check_expr(*e->else_expr, locals);
    expect(else_type, then_type, e->else_expr->loc);
    expr.inferred = then_type;
  } else if (auto* e = dynamic_cast<LetExpr*>(&expr)) {
    const TypeRef value_type = check_expr(*e->value, locals);
    auto nested = locals;
    nested[e->name] = value_type;
    expr.inferred = check_expr(*e->body, nested);
  } else if (auto* e = dynamic_cast<AssignExpr*>(&expr)) {
    const auto it = locals.find(e->name);
    if (it == locals.end()) {
      throw DiagnosticError(e->loc, "assignment to unknown variable '" + e->name + "'");
    }
    expect(check_expr(*e->value, locals), it->second, e->value->loc);
    expr.inferred = TypeRef::unit();
  } else if (auto* e = dynamic_cast<SequenceExpr*>(&expr)) {
    TypeRef last = TypeRef::unit();
    for (auto& item : e->expressions) {
      last = check_expr(*item, locals);
    }
    expr.inferred = last;
  } else if (auto* e = dynamic_cast<ForExpr*>(&expr)) {
    expect(check_expr(*e->start, locals), TypeRef::i32(), e->start->loc);
    expect(check_expr(*e->end, locals), TypeRef::i32(), e->end->loc);
    auto nested = locals;
    nested[e->var] = TypeRef::i32();
    check_expr(*e->body, nested);
    expr.inferred = check_expr(*e->result, locals);
  } else {
    throw DiagnosticError(expr.loc, "internal typechecker error");
  }
  return expr.inferred;
}

}  // namespace fluxion
