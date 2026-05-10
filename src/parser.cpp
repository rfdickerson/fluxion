#include "parser.h"

#include <utility>

namespace fluxion {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek(int offset) const {
  const auto index = pos_ + static_cast<std::size_t>(offset);
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::is_name_token(TokenKind kind) const {
  return kind == TokenKind::Identifier || kind == TokenKind::Input || kind == TokenKind::Output ||
         kind == TokenKind::Event || kind == TokenKind::Init || kind == TokenKind::Tick || kind == TokenKind::Phase ||
         kind == TokenKind::State || kind == TokenKind::Stream || kind == TokenKind::Sampled ||
         kind == TokenKind::Latest || kind == TokenKind::History || kind == TokenKind::Every ||
         kind == TokenKind::MaxAge || kind == TokenKind::Pipeline || kind == TokenKind::Runtime ||
         kind == TokenKind::Frame || kind == TokenKind::Measurement || kind == TokenKind::Contract ||
         kind == TokenKind::Controller || kind == TokenKind::Mpc || kind == TokenKind::Cbf ||
         kind == TokenKind::Clf || kind == TokenKind::WorldModel || kind == TokenKind::Source ||
         kind == TokenKind::Effects || kind == TokenKind::Constraint || kind == TokenKind::Assume ||
         kind == TokenKind::Guarantee || kind == TokenKind::Invariant || kind == TokenKind::OnTimeout ||
         kind == TokenKind::OnStaleState || kind == TokenKind::OnInfeasible ||
         kind == TokenKind::Region || kind == TokenKind::Reactor || kind == TokenKind::Before ||
         kind == TokenKind::After || kind == TokenKind::Safe || kind == TokenKind::Capacity ||
         kind == TokenKind::Overflow || kind == TokenKind::Deadline || kind == TokenKind::Priority ||
         kind == TokenKind::Parallel;
}

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
    } else if (match(TokenKind::Phase)) {
      module.phases.push_back(parse_phase_decl());
    } else if (check(TokenKind::Reactor)) {
      module.reactors.push_back(parse_reactor_decl());
    } else if (check(TokenKind::Pipeline)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Pipeline, TokenKind::Pipeline, true));
    } else if (check(TokenKind::Runtime)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Runtime, TokenKind::Runtime, true));
    } else if (check(TokenKind::Frame)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Frame, TokenKind::Frame, true));
    } else if (check(TokenKind::Measurement)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Measurement, TokenKind::Measurement, true));
    } else if (check(TokenKind::Contract)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Contract, TokenKind::Contract, false));
    } else if (check(TokenKind::Controller)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Controller, TokenKind::Controller, true));
    } else if (check(TokenKind::Mpc)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Mpc, TokenKind::Mpc, true));
    } else if (check(TokenKind::Cbf)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Cbf, TokenKind::Cbf, true));
    } else if (check(TokenKind::Clf)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Clf, TokenKind::Clf, true));
    } else if (check(TokenKind::WorldModel)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::WorldModel, TokenKind::WorldModel, true));
    } else if (check(TokenKind::Source)) {
      module.design_decls.push_back(parse_design_decl(DesignDecl::Kind::Source, TokenKind::Source, true));
    } else {
      throw DiagnosticError(peek().loc, "expected type, function, phase, reactor, pipeline, runtime, contract, model, or control declaration");
    }
  }
  return module;
}

const Token& Parser::consume_name(const std::string& message) {
  if (is_name_token(peek().kind)) {
    return tokens_[pos_++];
  }
  throw DiagnosticError(peek().loc, message);
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

TypeSyntax Parser::parse_type_syntax_until_reactor_clause() {
  if (check(TokenKind::End) || check(TokenKind::RBrace)) {
    throw DiagnosticError(peek().loc, "expected type");
  }
  const SourceLocation loc = peek().loc;
  std::string text;
  int angle_depth = 0;
  while (!check(TokenKind::End)) {
    if (angle_depth == 0 &&
        (check(TokenKind::Capacity) || check(TokenKind::Overflow) || check(TokenKind::In) || check(TokenKind::Assign) ||
         check(TokenKind::History) || check(TokenKind::MaxAge) ||
         check(TokenKind::Input) || check(TokenKind::Output) || check(TokenKind::State) || check(TokenKind::On) ||
         check(TokenKind::Tick) || check(TokenKind::Deadline) || check(TokenKind::RBrace))) {
      break;
    }
    Token token = peek();
    ++pos_;
    if (!text.empty() && token.kind != TokenKind::Dot && token.kind != TokenKind::Comma && token.kind != TokenKind::RBracket &&
        token.kind != TokenKind::Greater && token.kind != TokenKind::LBracket) {
      text += " ";
    }
    text += token.text;
    if (token.kind == TokenKind::Less) {
      ++angle_depth;
    } else if (token.kind == TokenKind::Greater && angle_depth > 0) {
      --angle_depth;
    }
  }
  if (text.empty()) {
    throw DiagnosticError(loc, "expected type");
  }
  return {text, loc};
}

TypeSyntax Parser::parse_type_syntax_until(TokenKind end) {
  if (check(TokenKind::End) || check(TokenKind::RBrace)) {
    throw DiagnosticError(peek().loc, "expected type");
  }
  const SourceLocation loc = peek().loc;
  std::string text;
  int angle_depth = 0;
  while (!check(TokenKind::End)) {
    if (angle_depth == 0 && check(end)) {
      break;
    }
    Token token = peek();
    ++pos_;
    if (!text.empty() && token.kind != TokenKind::Dot && token.kind != TokenKind::Comma && token.kind != TokenKind::RBracket &&
        token.kind != TokenKind::Greater && token.kind != TokenKind::LBracket) {
      text += " ";
    }
    text += token.text;
    if (token.kind == TokenKind::Less) {
      ++angle_depth;
    } else if (token.kind == TokenKind::Greater && angle_depth > 0) {
      --angle_depth;
    }
  }
  if (text.empty()) {
    throw DiagnosticError(loc, "expected type");
  }
  return {text, loc};
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

PhaseDecl Parser::parse_phase_decl() {
  const Token& name = consume_name("expected phase name");
  PhaseDecl phase{name.text, PhaseDecl::Relation::None, "", name.loc};
  if (match(TokenKind::Before)) {
    phase.relation = PhaseDecl::Relation::Before;
    phase.related_phase = consume_name("expected phase name after 'before'").text;
  } else if (match(TokenKind::After)) {
    phase.relation = PhaseDecl::Relation::After;
    phase.related_phase = consume_name("expected phase name after 'after'").text;
  }
  return phase;
}

std::string Parser::parse_dotted_measure() {
  const Token& number = consume(TokenKind::Number, "expected numeric measure");
  std::string text = number.text;
  if (match(TokenKind::Dot)) {
    text += "." + consume(TokenKind::Identifier, "expected unit suffix").text;
  }
  return text;
}

std::string Parser::append_token_text(std::string body, const Token& token) const {
  if (!body.empty() && token.kind != TokenKind::Dot && token.kind != TokenKind::Comma && token.kind != TokenKind::RParen &&
      token.kind != TokenKind::RBracket && token.kind != TokenKind::RBrace && token.kind != TokenKind::Semicolon) {
    body += " ";
  }
  body += token.text;
  return body;
}

std::string Parser::collect_balanced_block() {
  consume(TokenKind::LBrace, "expected '{'");
  std::string body = "{";
  int depth = 1;
  while (!check(TokenKind::End) && depth > 0) {
    Token token = peek();
    ++pos_;
    body = append_token_text(std::move(body), token);
    if (token.kind == TokenKind::LBrace) {
      ++depth;
    } else if (token.kind == TokenKind::RBrace) {
      --depth;
    }
  }
  if (depth != 0) {
    throw DiagnosticError(peek().loc, "expected '}' to close declaration body");
  }
  return body;
}

std::string Parser::collect_design_signature_until_body() {
  std::string signature;
  int paren_depth = 0;
  int bracket_depth = 0;
  while (!check(TokenKind::End)) {
    if ((check(TokenKind::LBrace) || check(TokenKind::Assign)) && paren_depth == 0 && bracket_depth == 0) {
      break;
    }
    Token token = peek();
    ++pos_;
    signature = append_token_text(std::move(signature), token);
    if (token.kind == TokenKind::LParen) {
      ++paren_depth;
    } else if (token.kind == TokenKind::RParen && paren_depth > 0) {
      --paren_depth;
    } else if (token.kind == TokenKind::LBracket) {
      ++bracket_depth;
    } else if (token.kind == TokenKind::RBracket && bracket_depth > 0) {
      --bracket_depth;
    }
  }
  return signature;
}

bool Parser::is_module_item_start() const {
  return check(TokenKind::Type) || check(TokenKind::Fn) || check(TokenKind::Phase) || check(TokenKind::Reactor) ||
         check(TokenKind::Pipeline) || check(TokenKind::Runtime) || check(TokenKind::Frame) ||
         check(TokenKind::Measurement) || check(TokenKind::Contract) || check(TokenKind::Controller) ||
         check(TokenKind::Mpc) || check(TokenKind::Cbf) || check(TokenKind::Clf) || check(TokenKind::WorldModel) ||
         check(TokenKind::Source);
}

std::string Parser::collect_design_expression_body(int item_column) {
  std::string body;
  int paren_depth = 0;
  int bracket_depth = 0;
  while (!check(TokenKind::End)) {
    if (peek().loc.column <= item_column && paren_depth == 0 && bracket_depth == 0 && is_module_item_start()) {
      break;
    }
    Token token = peek();
    ++pos_;
    body = append_token_text(std::move(body), token);
    if (token.kind == TokenKind::LParen) {
      ++paren_depth;
    } else if (token.kind == TokenKind::RParen && paren_depth > 0) {
      --paren_depth;
    } else if (token.kind == TokenKind::LBracket) {
      ++bracket_depth;
    } else if (token.kind == TokenKind::RBracket && bracket_depth > 0) {
      --bracket_depth;
    }
  }
  return body;
}

DesignDecl Parser::parse_design_decl(DesignDecl::Kind kind, TokenKind keyword, bool requires_name) {
  const Token& start = consume(keyword, "expected declaration keyword");
  DesignDecl decl;
  decl.kind = kind;
  decl.loc = start.loc;
  if (requires_name) {
    const Token& name = consume_name("expected declaration name");
    decl.name = name.text;
  } else if (!check(TokenKind::LBrace)) {
    decl.name = consume_name("expected declaration name").text;
  }
  decl.signature = collect_design_signature_until_body();
  if (match(TokenKind::Assign)) {
    decl.body = collect_design_expression_body(start.loc.column);
  } else {
    decl.body = collect_balanced_block();
  }
  if (decl.body.empty()) {
    throw DiagnosticError(start.loc, "declaration body must not be empty");
  }
  return decl;
}

ReactorDecl Parser::parse_reactor_decl() {
  consume(TokenKind::Reactor, "expected 'reactor'");
  const Token& name = consume_name("expected reactor name");
  ReactorDecl reactor;
  reactor.name = name.text;
  reactor.loc = name.loc;
  if (match(TokenKind::LParen)) {
    while (!check(TokenKind::RParen)) {
      const Token& key = consume_name("expected reactor metadata key");
      consume(TokenKind::Assign, "expected '=' after reactor metadata key");
      if (key.text == "period" || key.text == "tick") {
        reactor.meta.tick = parse_dotted_measure();
      } else if (key.text == "deadline") {
        reactor.meta.deadline = parse_dotted_measure();
      } else if (key.text == "priority") {
        const Token& priority = consume(TokenKind::Number, "expected numeric priority");
        if (priority.text.find('.') != std::string::npos) {
          throw DiagnosticError(priority.loc, "priority must be an integer");
        }
        reactor.meta.priority = std::stoi(priority.text);
        reactor.meta.has_priority = true;
      } else if (key.text == "phase") {
        reactor.meta.phase = consume_name("expected phase name").text;
      } else {
        throw DiagnosticError(key.loc, "unknown reactor metadata key '" + key.text + "'");
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    consume(TokenKind::RParen, "expected ')' after reactor metadata");
  }
  while (!check(TokenKind::LBrace)) {
    if (match(TokenKind::Phase)) {
      reactor.meta.phase = consume_name("expected phase name").text;
    } else if (match(TokenKind::In)) {
      reactor.meta.phase = consume_name("expected phase name").text;
    } else if (match(TokenKind::Every)) {
      reactor.meta.tick = parse_dotted_measure();
    } else if (match(TokenKind::Tick)) {
      reactor.meta.tick = parse_dotted_measure();
    } else if (match(TokenKind::Priority)) {
      const Token& priority = consume(TokenKind::Number, "expected numeric priority");
      if (priority.text.find('.') != std::string::npos) {
        throw DiagnosticError(priority.loc, "priority must be an integer");
      }
      reactor.meta.priority = std::stoi(priority.text);
      reactor.meta.has_priority = true;
    } else if (match(TokenKind::Deadline)) {
      reactor.meta.deadline = parse_dotted_measure();
    } else if (match(TokenKind::Parallel)) {
      consume(TokenKind::Safe, "expected 'safe' after 'parallel'");
      reactor.meta.parallel_safe = true;
    } else {
      throw DiagnosticError(peek().loc, "expected reactor metadata or '{'");
    }
  }
  consume(TokenKind::LBrace, "expected '{' in reactor");
  while (!check(TokenKind::RBrace) && !check(TokenKind::End)) {
    if (check(TokenKind::Input)) {
      reactor.ports.push_back(parse_reactor_port(ReactorPortDecl::Direction::Input));
    } else if (check(TokenKind::Output)) {
      reactor.ports.push_back(parse_reactor_port(ReactorPortDecl::Direction::Output));
    } else if (check(TokenKind::State)) {
      reactor.states.push_back(parse_reactor_state());
    } else if (match(TokenKind::Deadline)) {
      reactor.meta.deadline = parse_dotted_measure();
    } else if (check(TokenKind::On)) {
      reactor.handlers.push_back(parse_reactor_handler());
    } else if (check(TokenKind::Tick)) {
      const Token& start = consume(TokenKind::Tick, "expected 'tick'");
      ReactorHandlerDecl handler;
      handler.target = "tick";
      handler.loc = start.loc;
      handler.body = collect_handler_body(start.loc.column);
      if (handler.body.empty()) {
        throw DiagnosticError(start.loc, "tick body must not be empty");
      }
      reactor.handlers.push_back(std::move(handler));
    } else {
      throw DiagnosticError(peek().loc, "expected reactor input, output, state, tick, or handler");
    }
  }
  consume(TokenKind::RBrace, "expected '}' after reactor");
  return reactor;
}

ReactorPortDecl Parser::parse_reactor_port(ReactorPortDecl::Direction direction) {
  const Token& start = consume(direction == ReactorPortDecl::Direction::Input ? TokenKind::Input : TokenKind::Output,
                               "expected reactor port");
  const Token& name = consume_name("expected port name");
  consume(TokenKind::Colon, "expected ':' after port name");
  ReactorPortDecl port;
  port.direction = direction;
  port.name = name.text;
  port.loc = start.loc;
  if (match(TokenKind::Stream)) {
    port.kind = ReactorPortDecl::Kind::Stream;
  } else if (match(TokenKind::Sampled)) {
    port.kind = ReactorPortDecl::Kind::Sampled;
  } else if (match(TokenKind::Latest)) {
    port.kind = ReactorPortDecl::Kind::Latest;
  } else {
    port.kind = ReactorPortDecl::Kind::Sampled;
  }
  if (match(TokenKind::Less)) {
    port.type = parse_type_syntax_until(TokenKind::Greater);
    consume(TokenKind::Greater, "expected '>' after port type");
  } else {
    port.type = parse_type_syntax_until_reactor_clause();
  }
  while (check(TokenKind::Capacity) || check(TokenKind::Overflow) || check(TokenKind::History) || check(TokenKind::MaxAge) ||
         check(TokenKind::LBrace)) {
    if (match(TokenKind::Capacity)) {
      const Token& capacity = consume(TokenKind::Number, "expected stream capacity");
      if (capacity.text.find('.') != std::string::npos) {
        throw DiagnosticError(capacity.loc, "capacity must be an integer");
      }
      port.capacity = std::stoi(capacity.text);
    } else if (match(TokenKind::Overflow)) {
      port.overflow = consume_name("expected overflow policy").text;
    } else if (match(TokenKind::History)) {
      port.history = parse_dotted_measure();
    } else if (match(TokenKind::MaxAge)) {
      port.max_age = parse_dotted_measure();
    } else if (match(TokenKind::LBrace)) {
      while (!check(TokenKind::RBrace) && !check(TokenKind::End)) {
        if (match(TokenKind::Capacity)) {
          const Token& capacity = consume(TokenKind::Number, "expected stream capacity");
          if (capacity.text.find('.') != std::string::npos) {
            throw DiagnosticError(capacity.loc, "capacity must be an integer");
          }
          port.capacity = std::stoi(capacity.text);
        } else if (match(TokenKind::Overflow)) {
          port.overflow = consume_name("expected overflow policy").text;
        } else if (match(TokenKind::History)) {
          port.history = parse_dotted_measure();
        } else if (match(TokenKind::MaxAge)) {
          port.max_age = parse_dotted_measure();
        } else {
          throw DiagnosticError(peek().loc, "expected port capacity, overflow, history, or max_age");
        }
      }
      consume(TokenKind::RBrace, "expected '}' after port attributes");
    }
  }
  return port;
}

ReactorStateDecl Parser::parse_reactor_state() {
  const Token& start = consume(TokenKind::State, "expected 'state'");
  const Token& name = consume_name("expected state name");
  consume(TokenKind::Colon, "expected ':' after state name");
  ReactorStateDecl state;
  state.name = name.text;
  state.loc = start.loc;
  state.type = parse_type_syntax_until_reactor_clause();
  if (match(TokenKind::Assign)) {
    const int item_column = start.loc.column;
    while (!check(TokenKind::End) && !check(TokenKind::RBrace)) {
      if (peek().loc.column <= item_column && is_reactor_item_start()) {
        break;
      }
      Token token = peek();
      ++pos_;
      if (!state.initializer.empty()) {
        state.initializer += token.kind == TokenKind::Dot || token.kind == TokenKind::Comma ||
                                     token.kind == TokenKind::RParen || token.kind == TokenKind::RBracket
                                 ? ""
                                 : " ";
      }
      state.initializer += token.text;
    }
  }
  if (match(TokenKind::In)) {
    consume(TokenKind::Region, "expected 'region' after 'in'");
    state.region = consume_name("expected region name").text;
  }
  return state;
}

bool Parser::is_reactor_item_start() const {
  return check(TokenKind::Input) || check(TokenKind::Output) || check(TokenKind::State) || check(TokenKind::On) ||
         check(TokenKind::Tick) || check(TokenKind::Deadline);
}

std::string Parser::collect_handler_body(int item_column) {
  std::string body;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  while (!check(TokenKind::End)) {
    if (check(TokenKind::RBrace) && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
      break;
    }
    if (peek().loc.column <= item_column && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
        is_reactor_item_start()) {
      break;
    }
    Token token = peek();
    ++pos_;
    if (!body.empty()) {
      body += token.kind == TokenKind::Dot || token.kind == TokenKind::Comma || token.kind == TokenKind::RParen ||
                      token.kind == TokenKind::RBracket || token.kind == TokenKind::RBrace
                  ? ""
                  : " ";
    }
    body += token.text;
    if (token.kind == TokenKind::LParen) {
      ++paren_depth;
    } else if (token.kind == TokenKind::RParen && paren_depth > 0) {
      --paren_depth;
    } else if (token.kind == TokenKind::LBracket) {
      ++bracket_depth;
    } else if (token.kind == TokenKind::RBracket && bracket_depth > 0) {
      --bracket_depth;
    } else if (token.kind == TokenKind::LBrace) {
      ++brace_depth;
    } else if (token.kind == TokenKind::RBrace && brace_depth > 0) {
      --brace_depth;
    }
  }
  return body;
}

ReactorHandlerDecl Parser::parse_reactor_handler() {
  const Token& start = consume(TokenKind::On, "expected 'on'");
  ReactorHandlerDecl handler;
  handler.loc = start.loc;
  if (match(TokenKind::Event)) {
    handler.target = "event " + consume_name("expected event input name").text;
  } else if (match(TokenKind::Init)) {
    handler.target = "init";
  } else if (match(TokenKind::Tick)) {
    handler.target = "tick";
  } else {
    handler.target = consume_name("expected handler target").text;
  }
  if (match(TokenKind::LParen)) {
    if (!check(TokenKind::RParen)) {
      handler.parameter = consume_name("expected handler parameter name").text;
      if (match(TokenKind::Colon)) {
        parse_type_syntax_until(TokenKind::RParen);
      }
    }
    consume(TokenKind::RParen, "expected ')' after handler parameter");
  }
  consume(TokenKind::Assign, "expected '=' before handler body");
  handler.body = collect_handler_body(start.loc.column);
  if (handler.body.empty()) {
    throw DiagnosticError(start.loc, "handler body must not be empty");
  }
  return handler;
}

std::vector<Parameter> Parser::parse_params(TokenKind end) {
  std::vector<Parameter> params;
  if (check(end)) {
    return params;
  }
  do {
    const Token& name = consume_name("expected parameter or field name");
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
  const Token& name = consume_name("expected binding name");
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
  const Token& var = consume_name("expected loop variable");
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
  if (is_name_token(peek().kind) && peek(1).kind == TokenKind::Assign) {
    const Token& name = consume_name("expected assignment target");
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
  auto expr = parse_power();
  while (check(TokenKind::Star) || check(TokenKind::Slash)) {
    Token op = peek();
    ++pos_;
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr), parse_power());
  }
  return expr;
}

ExprPtr Parser::parse_power() {
  auto expr = parse_unary();
  if (check(TokenKind::Caret)) {
    Token op = peek();
    ++pos_;
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr), parse_power());
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
    const Token& field = consume_name("expected field name after '.'");
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
  if (is_name_token(peek().kind)) {
    Token name = tokens_[pos_++];
    if (match(TokenKind::LParen)) {
      if (is_name_token(peek().kind) && peek(1).kind == TokenKind::Colon) {
        auto record = std::make_unique<RecordLiteralExpr>(name.loc, name.text);
        do {
          const Token& field = consume_name("expected record field name");
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
          const Token& field = consume_name("expected record field name");
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
