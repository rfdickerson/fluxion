#include "ast.h"

#include <sstream>

namespace fluxion {

TypeRef TypeRef::i32() { return {TypeKind::I32, {}}; }
TypeRef TypeRef::f64() { return {TypeKind::F64, {}}; }
TypeRef TypeRef::boolean() { return {TypeKind::Bool, {}}; }
TypeRef TypeRef::string() { return {TypeKind::String, {}}; }
TypeRef TypeRef::matrix() { return {TypeKind::Matrix, {}}; }
TypeRef TypeRef::matrix(int rows, int cols) {
  TypeRef type = matrix();
  type.matrix_rows = rows;
  type.matrix_cols = cols;
  return type;
}
TypeRef TypeRef::unit() { return {TypeKind::Unit, {}}; }
TypeRef TypeRef::record(std::string name) { return {TypeKind::Record, std::move(name)}; }
TypeRef TypeRef::scalar_unit(std::string name) { return {TypeKind::ScalarUnit, std::move(name)}; }

bool TypeRef::is_numeric() const {
  return kind == TypeKind::I32 || kind == TypeKind::F64 || kind == TypeKind::ScalarUnit;
}

bool TypeRef::operator==(const TypeRef& other) const {
  return kind == other.kind && record_name == other.record_name && matrix_rows == other.matrix_rows &&
         matrix_cols == other.matrix_cols;
}

std::string TypeRef::str() const {
  switch (kind) {
    case TypeKind::I32:
      return "i32";
    case TypeKind::F64:
      return "f64";
    case TypeKind::Bool:
      return "bool";
    case TypeKind::String:
      return "string";
    case TypeKind::Matrix:
      if (matrix_rows >= 0 && matrix_cols >= 0) {
        std::ostringstream out;
        out << "Matrix[" << matrix_rows << ", " << matrix_cols << ", Double]";
        return out.str();
      }
      return "Matrix";
    case TypeKind::Unit:
      return "unit";
    case TypeKind::Record:
      return record_name;
    case TypeKind::ScalarUnit:
      return record_name;
    case TypeKind::Unknown:
      return "<unknown>";
  }
  return "<unknown>";
}

}  // namespace fluxion
