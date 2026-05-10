#pragma once

#include "diagnostic.h"

#include <string>
#include <vector>

namespace fluxion {

enum class TokenKind {
  End,
  Identifier,
  Number,
  String,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
  Comma,
  Colon,
  Dot,
  Plus,
  Minus,
  Star,
  Slash,
  Caret,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  BangEqual,
  Assign,
  Arrow,
  LeftArrow,
  Range,
  Semicolon,
  Module,
  Type,
  Fn,
  Let,
  In,
  If,
  Then,
  Else,
  For,
  Static,
  Do,
  Reactor,
  Phase,
  Before,
  After,
  Tick,
  Deadline,
  Priority,
  Parallel,
  Safe,
  Input,
  Output,
  Stream,
  Sampled,
  Latest,
  History,
  Every,
  MaxAge,
  State,
  Region,
  Overflow,
  Capacity,
  Pipeline,
  Runtime,
  Frame,
  Measurement,
  Contract,
  Controller,
  Mpc,
  Cbf,
  Clf,
  WorldModel,
  Source,
  Effects,
  Constraint,
  Assume,
  Guarantee,
  Invariant,
  OnTimeout,
  OnStaleState,
  OnInfeasible,
  On,
  Event,
  Init,
  Emit,
  True,
  False
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  SourceLocation loc;
};

class Lexer {
 public:
  Lexer(std::string file, std::string source);
  std::vector<Token> lex();

 private:
  char peek(int offset = 0) const;
  char advance();
  bool match(char c);
  void skip_whitespace_and_comments();
  Token make(TokenKind kind, std::string text, SourceLocation loc) const;
  Token identifier(SourceLocation loc);
  Token number(SourceLocation loc);
  Token string(SourceLocation loc);

  std::string file_;
  std::string source_;
  std::size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
};

}  // namespace fluxion
