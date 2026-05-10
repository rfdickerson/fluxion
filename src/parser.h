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
  bool is_name_token(TokenKind kind) const;
  bool match(TokenKind kind);
  const Token& consume(TokenKind kind, const std::string& message);

  TypeRef parse_type_ref();
  TypeSyntax parse_type_syntax_until_reactor_clause();
  TypeSyntax parse_type_syntax_until(TokenKind end);
  const Token& consume_name(const std::string& message);
  void parse_type_decl(Module& module);
  PhaseDecl parse_phase_decl();
  ReactorDecl parse_reactor_decl();
  ReactorPortDecl parse_reactor_port(ReactorPortDecl::Direction direction);
  ReactorStateDecl parse_reactor_state();
  ReactorHandlerDecl parse_reactor_handler();
  std::string parse_dotted_measure();
  std::string collect_handler_body(int item_column);
  bool is_reactor_item_start() const;
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
  ExprPtr parse_power();
  ExprPtr parse_unary();
  ExprPtr parse_postfix();
  ExprPtr parse_primary();
  int precedence(TokenKind kind) const;

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
};

}  // namespace fluxion
