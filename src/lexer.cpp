#include "lexer.h"

#include <cctype>
#include <unordered_map>

namespace fluxion {

Lexer::Lexer(std::string file, std::string source)
    : file_(std::move(file)), source_(std::move(source)) {}

char Lexer::peek(int offset) const {
  const auto index = pos_ + static_cast<std::size_t>(offset);
  return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
  const char c = peek();
  if (c == '\0') {
    return c;
  }
  ++pos_;
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Lexer::match(char c) {
  if (peek() != c) {
    return false;
  }
  advance();
  return true;
}

Token Lexer::make(TokenKind kind, std::string text, SourceLocation loc) const {
  return {kind, std::move(text), std::move(loc)};
}

void Lexer::skip_whitespace_and_comments() {
  for (;;) {
    while (std::isspace(static_cast<unsigned char>(peek()))) {
      advance();
    }
    if (peek() == '/' && peek(1) == '/') {
      while (peek() != '\0' && peek() != '\n') {
        advance();
      }
      continue;
    }
    return;
  }
}

Token Lexer::identifier(SourceLocation loc) {
  std::string text;
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    text.push_back(advance());
  }
  static const std::unordered_map<std::string, TokenKind> keywords = {
      {"module", TokenKind::Module}, {"struct", TokenKind::Type}, {"type", TokenKind::Type}, {"func", TokenKind::Fn},
      {"let", TokenKind::Let},       {"in", TokenKind::In},       {"if", TokenKind::If},
      {"then", TokenKind::Then},     {"else", TokenKind::Else},   {"for", TokenKind::For},
      {"static", TokenKind::Static}, {"do", TokenKind::Do},       {"true", TokenKind::True},
      {"false", TokenKind::False},
  };
  const auto it = keywords.find(text);
  return make(it == keywords.end() ? TokenKind::Identifier : it->second, text, std::move(loc));
}

Token Lexer::number(SourceLocation loc) {
  std::string text;
  while (std::isdigit(static_cast<unsigned char>(peek()))) {
    text.push_back(advance());
  }
  if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
    text.push_back(advance());
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
      text.push_back(advance());
    }
  }
  return make(TokenKind::Number, text, std::move(loc));
}

Token Lexer::string(SourceLocation loc) {
  std::string text;
  while (peek() != '\0' && peek() != '"') {
    const char c = advance();
    if (c == '\\') {
      const char escaped = advance();
      switch (escaped) {
        case '"':
          text.push_back('"');
          break;
        case '\\':
          text.push_back('\\');
          break;
        case 'n':
          text.push_back('\n');
          break;
        case 't':
          text.push_back('\t');
          break;
        default:
          throw DiagnosticError(loc, "unsupported string escape");
      }
    } else {
      text.push_back(c);
    }
  }
  if (!match('"')) {
    throw DiagnosticError(loc, "unterminated string literal");
  }
  return make(TokenKind::String, text, std::move(loc));
}

std::vector<Token> Lexer::lex() {
  std::vector<Token> tokens;
  for (;;) {
    skip_whitespace_and_comments();
    SourceLocation loc{file_, line_, column_};
    const char c = peek();
    if (c == '\0') {
      tokens.push_back(make(TokenKind::End, "", loc));
      return tokens;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      tokens.push_back(identifier(loc));
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      tokens.push_back(number(loc));
      continue;
    }
    advance();
    switch (c) {
      case '"':
        tokens.push_back(string(loc));
        break;
      case '(':
        tokens.push_back(make(TokenKind::LParen, "(", loc));
        break;
      case ')':
        tokens.push_back(make(TokenKind::RParen, ")", loc));
        break;
      case '[':
        tokens.push_back(make(TokenKind::LBracket, "[", loc));
        break;
      case ']':
        tokens.push_back(make(TokenKind::RBracket, "]", loc));
        break;
      case '{':
        tokens.push_back(make(TokenKind::LBrace, "{", loc));
        break;
      case '}':
        tokens.push_back(make(TokenKind::RBrace, "}", loc));
        break;
      case ',':
        tokens.push_back(make(TokenKind::Comma, ",", loc));
        break;
      case ':':
        tokens.push_back(make(TokenKind::Colon, ":", loc));
        break;
      case ';':
        tokens.push_back(make(TokenKind::Semicolon, ";", loc));
        break;
      case '.':
        if (match('.')) {
          tokens.push_back(make(TokenKind::Range, "..", loc));
        } else {
          tokens.push_back(make(TokenKind::Dot, ".", loc));
        }
        break;
      case '+':
        tokens.push_back(make(TokenKind::Plus, "+", loc));
        break;
      case '-':
        if (match('>')) {
          tokens.push_back(make(TokenKind::Arrow, "->", loc));
        } else {
          tokens.push_back(make(TokenKind::Minus, "-", loc));
        }
        break;
      case '*':
        tokens.push_back(make(TokenKind::Star, "*", loc));
        break;
      case '/':
        tokens.push_back(make(TokenKind::Slash, "/", loc));
        break;
      case '<':
        if (match('=')) {
          tokens.push_back(make(TokenKind::LessEqual, "<=", loc));
        } else {
          tokens.push_back(make(TokenKind::Less, "<", loc));
        }
        break;
      case '>':
        if (match('=')) {
          tokens.push_back(make(TokenKind::GreaterEqual, ">=", loc));
        } else {
          tokens.push_back(make(TokenKind::Greater, ">", loc));
        }
        break;
      case '=':
        if (match('=')) {
          tokens.push_back(make(TokenKind::EqualEqual, "==", loc));
        } else {
          tokens.push_back(make(TokenKind::Assign, "=", loc));
        }
        break;
      case '!':
        if (!match('=')) {
          throw DiagnosticError(loc, "expected '=' after '!'");
        }
        tokens.push_back(make(TokenKind::BangEqual, "!=", loc));
        break;
      default:
        throw DiagnosticError(loc, std::string("unexpected character '") + c + "'");
    }
  }
}

}  // namespace fluxion
