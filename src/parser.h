#pragma once

#include "ast.h"
#include "lexer.h"

#include <vector>

namespace fluxion {

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens);
  Module parse_module();

 private:
  const Token& peek(int offset = 0) const;
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  const Token& consume(TokenKind kind, const std::string& message);

  TypeRef parse_type_ref();
  void parse_type_decl(Module& module);
  RecordDecl parse_record_decl();
  FunctionDecl parse_function_decl();
  std::vector<Parameter> parse_params(TokenKind end);
  ExprPtr parse_expr();
  ExprPtr parse_let();
  ExprPtr parse_if();
  ExprPtr parse_for();
  ExprPtr parse_matrix();
  ExprPtr parse_sequence();
  ExprPtr parse_assignment();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_postfix();
  ExprPtr parse_primary();
  int precedence(TokenKind kind) const;

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
};

}  // namespace fluxion
