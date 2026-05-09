#pragma once

#include "diagnostic.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fluxion {

enum class TypeKind { I32, F64, Bool, String, Matrix, Unit, Record, ScalarUnit, Unknown };

struct TypeRef {
  TypeKind kind = TypeKind::Unknown;
  std::string record_name;
  int matrix_rows = -1;
  int matrix_cols = -1;

  static TypeRef i32();
  static TypeRef f64();
  static TypeRef boolean();
  static TypeRef string();
  static TypeRef matrix();
  static TypeRef matrix(int rows, int cols);
  static TypeRef unit();
  static TypeRef record(std::string name);
  static TypeRef scalar_unit(std::string name);
  bool is_numeric() const;
  bool operator==(const TypeRef& other) const;
  bool operator!=(const TypeRef& other) const { return !(*this == other); }
  std::string str() const;
};

struct Expr {
  explicit Expr(SourceLocation loc) : loc(std::move(loc)) {}
  virtual ~Expr() = default;
  SourceLocation loc;
  TypeRef inferred;
};

using ExprPtr = std::unique_ptr<Expr>;

struct NumberExpr : Expr {
  NumberExpr(SourceLocation loc, std::string text, bool integer)
      : Expr(std::move(loc)), text(std::move(text)), integer(integer) {}
  std::string text;
  bool integer;
};

struct BoolExpr : Expr {
  BoolExpr(SourceLocation loc, bool value) : Expr(std::move(loc)), value(value) {}
  bool value;
};

struct StringExpr : Expr {
  StringExpr(SourceLocation loc, std::string value) : Expr(std::move(loc)), value(std::move(value)) {}
  std::string value;
};

struct MatrixExpr : Expr {
  explicit MatrixExpr(SourceLocation loc) : Expr(std::move(loc)) {}
  std::vector<std::vector<ExprPtr>> rows;
};

struct VarExpr : Expr {
  VarExpr(SourceLocation loc, std::string name) : Expr(std::move(loc)), name(std::move(name)) {}
  std::string name;
};

struct BinaryExpr : Expr {
  BinaryExpr(SourceLocation loc, std::string op, ExprPtr lhs, ExprPtr rhs)
      : Expr(std::move(loc)), op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  std::string op;
  ExprPtr lhs;
  ExprPtr rhs;
};

struct CallExpr : Expr {
  CallExpr(SourceLocation loc, std::string callee) : Expr(std::move(loc)), callee(std::move(callee)) {}
  std::string callee;
  std::vector<ExprPtr> args;
};

struct FieldExpr : Expr {
  FieldExpr(SourceLocation loc, ExprPtr object, std::string field)
      : Expr(std::move(loc)), object(std::move(object)), field(std::move(field)) {}
  ExprPtr object;
  std::string field;
};

struct RecordLiteralExpr : Expr {
  RecordLiteralExpr(SourceLocation loc, std::string type_name)
      : Expr(std::move(loc)), type_name(std::move(type_name)) {}
  std::string type_name;
  std::vector<std::pair<std::string, ExprPtr>> fields;
};

struct IfExpr : Expr {
  IfExpr(SourceLocation loc, ExprPtr condition, ExprPtr then_expr, ExprPtr else_expr)
      : Expr(std::move(loc)),
        condition(std::move(condition)),
        then_expr(std::move(then_expr)),
        else_expr(std::move(else_expr)) {}
  ExprPtr condition;
  ExprPtr then_expr;
  ExprPtr else_expr;
};

struct LetExpr : Expr {
  LetExpr(SourceLocation loc, std::string name, ExprPtr value, ExprPtr body)
      : Expr(std::move(loc)), name(std::move(name)), value(std::move(value)), body(std::move(body)) {}
  std::string name;
  ExprPtr value;
  ExprPtr body;
};

struct ForExpr : Expr {
  ForExpr(SourceLocation loc, std::string var, ExprPtr start, ExprPtr end, ExprPtr body, ExprPtr result)
      : Expr(std::move(loc)),
        var(std::move(var)),
        start(std::move(start)),
        end(std::move(end)),
        body(std::move(body)),
        result(std::move(result)) {}
  std::string var;
  ExprPtr start;
  ExprPtr end;
  ExprPtr body;
  ExprPtr result;
};

struct AssignExpr : Expr {
  AssignExpr(SourceLocation loc, std::string name, ExprPtr value)
      : Expr(std::move(loc)), name(std::move(name)), value(std::move(value)) {}
  std::string name;
  ExprPtr value;
};

struct SequenceExpr : Expr {
  explicit SequenceExpr(SourceLocation loc) : Expr(std::move(loc)) {}
  std::vector<ExprPtr> expressions;
};

struct Parameter {
  std::string name;
  TypeRef type;
  SourceLocation loc;
};

struct FunctionDecl {
  std::string name;
  std::vector<Parameter> params;
  TypeRef return_type;
  ExprPtr body;
  SourceLocation loc;
};

struct RecordDecl {
  std::string name;
  std::vector<Parameter> fields;
  SourceLocation loc;
};

struct TypeAliasDecl {
  std::string name;
  TypeRef target;
  SourceLocation loc;
};

struct TypeSyntax {
  std::string text;
  SourceLocation loc;
};

struct PhaseDecl {
  enum class Relation { None, Before, After };

  std::string name;
  Relation relation = Relation::None;
  std::string related_phase;
  SourceLocation loc;
};

struct ReactorMeta {
  std::string phase;
  std::string tick;
  int priority = 0;
  bool has_priority = false;
  std::string deadline;
  bool parallel_safe = false;
};

struct ReactorPortDecl {
  enum class Direction { Input, Output };
  enum class Kind { Stream, Sampled };

  Direction direction = Direction::Input;
  std::string name;
  Kind kind = Kind::Stream;
  TypeSyntax type;
  int capacity = -1;
  std::string overflow;
  SourceLocation loc;
};

struct ReactorStateDecl {
  std::string name;
  TypeSyntax type;
  std::string region;
  SourceLocation loc;
};

struct ReactorHandlerDecl {
  std::string target;
  std::string parameter;
  std::string body;
  SourceLocation loc;
};

struct ReactorDecl {
  std::string name;
  ReactorMeta meta;
  std::vector<ReactorPortDecl> ports;
  std::vector<ReactorStateDecl> states;
  std::vector<ReactorHandlerDecl> handlers;
  SourceLocation loc;
};

struct Module {
  std::string name;
  std::vector<TypeAliasDecl> aliases;
  std::vector<RecordDecl> records;
  std::vector<FunctionDecl> functions;
  std::vector<PhaseDecl> phases;
  std::vector<ReactorDecl> reactors;
};

}  // namespace fluxion
