#include "parser.h"

#include <utility>

namespace fluxion {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek(int offset) const {
  const auto index = pos_ + static_cast<std::size_t>(offset);
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  ++pos_;
  return true;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
  if (!check(kind)) {
    throw DiagnosticError(peek().loc, message);
  }
  return tokens_[pos_++];
}

Module Parser::parse_module() {
  Module module;
  consume(TokenKind::Module, "expected 'module'");
  module.name = consume(TokenKind::Identifier, "expected module name").text;
  while (match(TokenKind::Dot)) {
    module.name += "." + consume(TokenKind::Identifier, "expected module name segment").text;
  }
  while (!check(TokenKind::End)) {
    if (match(TokenKind::Type)) {
      parse_type_decl(module);
    } else if (match(TokenKind::Fn)) {
      module.functions.push_back(parse_function_decl());
    } else {
      throw DiagnosticError(peek().loc, "expected struct or function declaration");
    }
  }
  return module;
}

TypeRef Parser::parse_type_ref() {
  const Token& token = consume(TokenKind::Identifier, "expected type name");
  if (token.text == "Int") {
    return TypeRef::i32();
  }
  if (token.text == "Double" || token.text == "Float") {
    return TypeRef::f64();
  }
  if (token.text == "Bool") {
    return TypeRef::boolean();
  }
  if (token.text == "String") {
    return TypeRef::string();
  }
  if (token.text == "Matrix") {
    if (!match(TokenKind::LBracket)) {
      return TypeRef::matrix();
    }
    const Token& rows = consume(TokenKind::Number, "expected matrix row count");
    consume(TokenKind::Comma, "expected ',' after matrix row count");
    const Token& cols = consume(TokenKind::Number, "expected matrix column count");
    consume(TokenKind::Comma, "expected ',' after matrix column count");
    const TypeRef element = parse_type_ref();
    if (element.kind != TypeKind::F64) {
      throw DiagnosticError(token.loc, "matrix element type must be Double");
    }
    consume(TokenKind::RBracket, "expected ']' after matrix type");
    if (rows.text.find('.') != std::string::npos || cols.text.find('.') != std::string::npos) {
      throw DiagnosticError(token.loc, "matrix dimensions must be integer constants");
    }
    return TypeRef::matrix(std::stoi(rows.text), std::stoi(cols.text));
  }
  if (token.text == "Void") {
    return TypeRef::unit();
  }
  return TypeRef::record(token.text);
}

void Parser::parse_type_decl(Module& module) {
  const Token& name = consume(TokenKind::Identifier, "expected type name");
  if (check(TokenKind::LBrace)) {
    --pos_;
    module.records.push_back(parse_record_decl());
    return;
  }
  consume(TokenKind::Assign, "expected '{' for struct or '=' for type alias");
  module.aliases.push_back({name.text, parse_type_ref(), name.loc});
}

std::vector<Parameter> Parser::parse_params(TokenKind end) {
  std::vector<Parameter> params;
  if (check(end)) {
    return params;
  }
  do {
    const Token& name = consume(TokenKind::Identifier, "expected parameter or field name");
    consume(TokenKind::Colon, "expected ':'");
    params.push_back({name.text, parse_type_ref(), name.loc});
  } while (match(TokenKind::Comma));
  return params;
}

RecordDecl Parser::parse_record_decl() {
  const Token& name = consume(TokenKind::Identifier, "expected record name");
  consume(TokenKind::LBrace, "expected '{' in record type");
  auto fields = parse_params(TokenKind::RBrace);
  consume(TokenKind::RBrace, "expected '}' after record type");
  return {name.text, std::move(fields), name.loc};
}

FunctionDecl Parser::parse_function_decl() {
  const Token& name = consume(TokenKind::Identifier, "expected function name");
  consume(TokenKind::LParen, "expected '(' after function name");
  auto params = parse_params(TokenKind::RParen);
  consume(TokenKind::RParen, "expected ')' after parameters");
  consume(TokenKind::Arrow, "expected '->' before return type");
  TypeRef return_type = parse_type_ref();
  consume(TokenKind::Assign, "expected '=' before function body");
  auto body = parse_expr();
  return {name.text, std::move(params), return_type, std::move(body), name.loc};
}

ExprPtr Parser::parse_expr() {
  if (check(TokenKind::Let)) {
    return parse_let();
  }
  if (check(TokenKind::If)) {
    return parse_if();
  }
  if (check(TokenKind::For)) {
    return parse_for();
  }
  return parse_sequence();
}

ExprPtr Parser::parse_let() {
  const Token& start = consume(TokenKind::Let, "expected 'let'");
  const Token& name = consume(TokenKind::Identifier, "expected binding name");
  consume(TokenKind::Assign, "expected '=' in let binding");
  auto value = parse_expr();
  consume(TokenKind::In, "expected 'in' after let value");
  auto body = parse_expr();
  return std::make_unique<LetExpr>(start.loc, name.text, std::move(value), std::move(body));
}

ExprPtr Parser::parse_if() {
  const Token& start = consume(TokenKind::If, "expected 'if'");
  auto condition = parse_expr();
  consume(TokenKind::Then, "expected 'then'");
  auto then_expr = parse_expr();
  consume(TokenKind::Else, "expected 'else'");
  auto else_expr = parse_expr();
  return std::make_unique<IfExpr>(start.loc, std::move(condition), std::move(then_expr), std::move(else_expr));
}

ExprPtr Parser::parse_for() {
  const Token& start = consume(TokenKind::For, "expected 'for'");
  const Token& var = consume(TokenKind::Identifier, "expected loop variable");
  consume(TokenKind::In, "expected 'in' after loop variable");
  auto begin = parse_expr();
  consume(TokenKind::Range, "expected '..' in loop range");
  auto end = parse_expr();
  consume(TokenKind::Static, "expected 'static' after bounded loop range");
  consume(TokenKind::Do, "expected 'do' before loop body");
  auto body = parse_expr();
  consume(TokenKind::In, "expected 'in' after loop body");
  auto result = parse_expr();
  return std::make_unique<ForExpr>(start.loc, var.text, std::move(begin), std::move(end), std::move(body), std::move(result));
}

ExprPtr Parser::parse_matrix() {
  const Token& start = consume(TokenKind::LBracket, "expected '['");
  auto matrix = std::make_unique<MatrixExpr>(start.loc);
  if (match(TokenKind::RBracket)) {
    return matrix;
  }
  for (;;) {
    std::vector<ExprPtr> row;
    row.push_back(parse_comparison());
    while (match(TokenKind::Comma)) {
      if (check(TokenKind::RBracket) || check(TokenKind::Semicolon)) {
        break;
      }
      row.push_back(parse_comparison());
    }
    matrix->rows.push_back(std::move(row));
    if (match(TokenKind::Semicolon)) {
      if (check(TokenKind::RBracket)) {
        break;
      }
      continue;
    }
    break;
  }
  consume(TokenKind::RBracket, "expected ']' after matrix literal");
  return matrix;
}

ExprPtr Parser::parse_sequence() {
  auto first = parse_assignment();
  if (!match(TokenKind::Semicolon)) {
    return first;
  }
  auto seq = std::make_unique<SequenceExpr>(first->loc);
  seq->expressions.push_back(std::move(first));
  do {
    seq->expressions.push_back(parse_expr());
  } while (match(TokenKind::Semicolon));
  return seq;
}

ExprPtr Parser::parse_assignment() {
  if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Assign) {
    const Token& name = consume(TokenKind::Identifier, "expected assignment target");
    consume(TokenKind::Assign, "expected '='");
    return std::make_unique<AssignExpr>(name.loc, name.text, parse_comparison());
  }
  return parse_comparison();
}

ExprPtr Parser::parse_comparison() {
  auto expr = parse_term();
  while (check(TokenKind::Less) || check(TokenKind::LessEqual) || check(TokenKind::Greater) ||
         check(TokenKind::GreaterEqual) || check(TokenKind::EqualEqual) || check(TokenKind::BangEqual)) {
    Token op = peek();
    ++pos_;
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr), parse_term());
  }
  return expr;
}

ExprPtr Parser::parse_term() {
  auto expr = parse_factor();
  while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
    Token op = peek();
    ++pos_;
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr), parse_factor());
  }
  return expr;
}

ExprPtr Parser::parse_factor() {
  auto expr = parse_unary();
  while (check(TokenKind::Star) || check(TokenKind::Slash)) {
    Token op = peek();
    ++pos_;
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr), parse_unary());
  }
  return expr;
}

ExprPtr Parser::parse_unary() {
  if (match(TokenKind::Minus)) {
    Token op = tokens_[pos_ - 1];
    if (match(TokenKind::Number)) {
      Token number = tokens_[pos_ - 1];
      return std::make_unique<NumberExpr>(op.loc, "-" + number.text, number.text.find('.') == std::string::npos);
    }
    auto zero = std::make_unique<NumberExpr>(op.loc, "0", true);
    return std::make_unique<BinaryExpr>(op.loc, "-", std::move(zero), parse_unary());
  }
  return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
  auto expr = parse_primary();
  while (match(TokenKind::Dot)) {
    const Token& field = consume(TokenKind::Identifier, "expected field name after '.'");
    expr = std::make_unique<FieldExpr>(field.loc, std::move(expr), field.text);
  }
  return expr;
}

ExprPtr Parser::parse_primary() {
  if (match(TokenKind::Number)) {
    const Token& token = tokens_[pos_ - 1];
    return std::make_unique<NumberExpr>(token.loc, token.text, token.text.find('.') == std::string::npos);
  }
  if (match(TokenKind::String)) {
    const Token& token = tokens_[pos_ - 1];
    return std::make_unique<StringExpr>(token.loc, token.text);
  }
  if (check(TokenKind::LBracket)) {
    return parse_matrix();
  }
  if (match(TokenKind::True)) {
    return std::make_unique<BoolExpr>(tokens_[pos_ - 1].loc, true);
  }
  if (match(TokenKind::False)) {
    return std::make_unique<BoolExpr>(tokens_[pos_ - 1].loc, false);
  }
  if (match(TokenKind::Identifier)) {
    Token name = tokens_[pos_ - 1];
    if (match(TokenKind::LParen)) {
      if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Colon) {
        auto record = std::make_unique<RecordLiteralExpr>(name.loc, name.text);
        do {
          const Token& field = consume(TokenKind::Identifier, "expected record field name");
          consume(TokenKind::Colon, "expected ':' in record literal");
          record->fields.push_back({field.text, parse_expr()});
        } while (match(TokenKind::Comma));
        consume(TokenKind::RParen, "expected ')' after record literal");
        return record;
      }
      auto call = std::make_unique<CallExpr>(name.loc, name.text);
      if (!check(TokenKind::RParen)) {
        do {
          call->args.push_back(parse_expr());
        } while (match(TokenKind::Comma));
      }
      consume(TokenKind::RParen, "expected ')' after call arguments");
      return call;
    }
    if (match(TokenKind::LBrace)) {
      auto record = std::make_unique<RecordLiteralExpr>(name.loc, name.text);
      if (!check(TokenKind::RBrace)) {
        do {
          const Token& field = consume(TokenKind::Identifier, "expected record field name");
          consume(TokenKind::Colon, "expected ':' in record literal");
          record->fields.push_back({field.text, parse_expr()});
        } while (match(TokenKind::Comma));
      }
      consume(TokenKind::RBrace, "expected '}' after record literal");
      return record;
    }
    return std::make_unique<VarExpr>(name.loc, name.text);
  }
  if (match(TokenKind::LParen)) {
    auto expr = parse_expr();
    consume(TokenKind::RParen, "expected ')'");
    return expr;
  }
  throw DiagnosticError(peek().loc, "expected expression");
}

}  // namespace fluxion
