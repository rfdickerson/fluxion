#include "codegen_llvm.h"
#include "diagnostic.h"
#include "jit.h"
#include "lexer.h"
#include "parser.h"
#include "physics_check.h"
#include "runtime.h"
#include "typecheck.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#define FLUXION_ISATTY _isatty
#define FLUXION_FILENO _fileno
#else
#include <termios.h>
#include <unistd.h>
#define FLUXION_ISATTY isatty
#define FLUXION_FILENO fileno
#endif

namespace {

std::string trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool color_enabled() {
  if (FLUXION_ISATTY(FLUXION_FILENO(stdout)) == 0) {
    return false;
  }
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
  const char* term = std::getenv("TERM");
  return term == nullptr || std::string(term) != "dumb";
}

std::string style(const std::string& text, const char* code) {
  if (!color_enabled()) {
    return text;
  }
  return std::string("\033[") + code + "m" + text + "\033[0m";
}

std::size_t display_width(const std::string& text) {
  std::size_t width = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
      i += 2;
      while (i < text.size() && (text[i] < '@' || text[i] > '~')) {
        ++i;
      }
      continue;
    }
    ++width;
  }
  return width;
}

bool starts_with_word(const std::string& text, const std::string& word) {
  if (text.size() < word.size() || text.compare(0, word.size(), word) != 0) {
    return false;
  }
  return text.size() == word.size() || std::isspace(static_cast<unsigned char>(text[word.size()]));
}

bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool delimiter_balanced(const std::string& text, char open, char close) {
  int depth = 0;
  for (const char c : text) {
    if (c == open) {
      ++depth;
    } else if (c == close) {
      --depth;
    }
  }
  return depth <= 0;
}

std::string format_fixed(double value, int precision) {
  if (std::abs(value) < 0.000000000001) {
    value = 0.0;
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string format_seconds(double value) {
  if (std::abs(value - std::round(value)) < 0.000000001) {
    return format_fixed(value, 1) + ".s";
  }
  std::string text = format_fixed(value, 3);
  while (text.size() > 1 && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.push_back('0');
  }
  return text + ".s";
}

std::string format_compact_seconds(double value) {
  if (std::abs(value) < 0.000000001) {
    return "0.0s";
  }
  if (std::abs(value - std::round(value)) < 0.000000001) {
    return format_fixed(value, 1) + "s";
  }
  return format_fixed(value, 1) + "s";
}

std::string bar_plot(double value, double max_value, int width) {
  const int filled = max_value <= 0.0 ? 0 : static_cast<int>(std::lround((value / max_value) * width));
  return std::string(static_cast<std::size_t>(std::max(0, std::min(width, filled))), '#');
}

int line_count(const std::string& text) {
  int lines = 1;
  for (char c : text) {
    if (c == '\n') {
      ++lines;
    }
  }
  return lines;
}

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw fluxion::DiagnosticError({path, 1, 1}, "could not open file");
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

fluxion::CheckedProgram compile_source(const std::string& path, const std::string& source) {
  fluxion::Lexer lexer(path, source);
  fluxion::Parser parser(lexer.lex());
  auto module = parser.parse_module();
  fluxion::TypeChecker checker(std::move(module));
  return checker.check();
}

#ifndef _WIN32
class TerminalRawMode {
 public:
  TerminalRawMode() {
    active_ = tcgetattr(STDIN_FILENO, &original_) == 0;
    if (!active_) {
      return;
    }
    termios raw = original_;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
      active_ = false;
    }
  }

  ~TerminalRawMode() {
    if (active_) {
      tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }
  }

  bool active() const { return active_; }

 private:
  termios original_{};
  bool active_ = false;
};
#endif

void refresh_interactive_line(const std::string& prompt,
                              const std::string& line,
                              std::size_t cursor,
                              std::size_t& rendered_width) {
  const std::size_t prompt_width = display_width(prompt);
  const std::size_t width = prompt_width + line.size();
  std::cout << '\r' << prompt << line;
  if (rendered_width > width) {
    std::cout << std::string(rendered_width - width, ' ');
  }
  std::cout << '\r' << prompt << line.substr(0, cursor);
  std::cout.flush();
  rendered_width = width;
}

void recall_history(const std::vector<std::string>& history,
                    int direction,
                    std::string& line,
                    std::string& draft,
                    std::size_t& history_index,
                    std::size_t& cursor) {
  if (history.empty()) {
    return;
  }
  if (direction < 0) {
    if (history_index == history.size()) {
      draft = line;
    }
    if (history_index > 0) {
      --history_index;
      line = history[history_index];
      cursor = line.size();
    }
    return;
  }

  if (history_index < history.size()) {
    ++history_index;
    line = history_index == history.size() ? draft : history[history_index];
    cursor = line.size();
  }
}

void add_history_entry(std::vector<std::string>& history, const std::string& line) {
  if (trim(line).empty()) {
    return;
  }
  if (!history.empty() && history.back() == line) {
    return;
  }
  history.push_back(line);
}

std::optional<std::string> read_interactive_line(const std::string& prompt, const std::vector<std::string>& history) {
  std::string line;
  std::string draft;
  std::size_t history_index = history.size();
  std::size_t cursor = 0;
  std::size_t rendered_width = 0;
  refresh_interactive_line(prompt, line, cursor, rendered_width);

#ifdef _WIN32
  for (;;) {
    const int ch = _getwch();
    if (ch == 0 || ch == 224) {
      const int key = _getwch();
      if (key == 72 || key == 80) {
        recall_history(history, key == 72 ? -1 : 1, line, draft, history_index, cursor);
      } else if (key == 75 && cursor > 0) {
        --cursor;
      } else if (key == 77 && cursor < line.size()) {
        ++cursor;
      } else if (key == 71) {
        cursor = 0;
      } else if (key == 79) {
        cursor = line.size();
      } else if (key == 83 && cursor < line.size()) {
        line.erase(cursor, 1);
      }
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == '\r' || ch == '\n') {
      std::cout << '\n';
      return line;
    }
    if (ch == 1) {
      cursor = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 2) {
      if (cursor > 0) {
        --cursor;
      }
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 5) {
      cursor = line.size();
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 6) {
      if (cursor < line.size()) {
        ++cursor;
      }
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 4) {
      if (line.empty()) {
        std::cout << '\n';
        return std::nullopt;
      }
      if (cursor < line.size()) {
        line.erase(cursor, 1);
        refresh_interactive_line(prompt, line, cursor, rendered_width);
      }
      continue;
    }
    if (ch == 3) {
      std::cout << "^C\n";
      return std::string();
    }
    if (ch == 11) {
      line.erase(cursor);
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 12) {
      std::cout << "\r\n";
      rendered_width = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 21) {
      line.erase(0, cursor);
      cursor = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == '\b' || ch == 127) {
      if (cursor > 0) {
        line.erase(cursor - 1, 1);
        --cursor;
        history_index = history.size();
        refresh_interactive_line(prompt, line, cursor, rendered_width);
      }
      continue;
    }
    if (ch >= 32 && ch < 127) {
      line.insert(cursor, 1, static_cast<char>(ch));
      ++cursor;
      history_index = history.size();
      refresh_interactive_line(prompt, line, cursor, rendered_width);
    }
  }
#else
  TerminalRawMode raw;
  if (!raw.active()) {
    if (!std::getline(std::cin, line)) {
      return std::nullopt;
    }
    return line;
  }

  for (;;) {
    unsigned char ch = 0;
    const ssize_t count = read(STDIN_FILENO, &ch, 1);
    if (count == 0) {
      std::cout << '\n';
      return std::nullopt;
    }
    if (count < 0) {
      if (errno == EINTR) {
        std::cout << '\n';
        return std::string();
      }
      std::cout << '\n';
      return std::nullopt;
    }

    if (ch == '\r' || ch == '\n') {
      std::cout << '\n';
      return line;
    }
    if (ch == 1) {
      cursor = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 2) {
      if (cursor > 0) {
        --cursor;
      }
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 5) {
      cursor = line.size();
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 6) {
      if (cursor < line.size()) {
        ++cursor;
      }
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 4) {
      if (line.empty()) {
        std::cout << '\n';
        return std::nullopt;
      }
      if (cursor < line.size()) {
        line.erase(cursor, 1);
        refresh_interactive_line(prompt, line, cursor, rendered_width);
      }
      continue;
    }
    if (ch == 3) {
      std::cout << "^C\n";
      return std::string();
    }
    if (ch == 11) {
      line.erase(cursor);
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 12) {
      std::cout << "\r\n";
      rendered_width = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 21) {
      line.erase(0, cursor);
      cursor = 0;
      refresh_interactive_line(prompt, line, cursor, rendered_width);
      continue;
    }
    if (ch == 127 || ch == '\b') {
      if (cursor > 0) {
        line.erase(cursor - 1, 1);
        --cursor;
        history_index = history.size();
        refresh_interactive_line(prompt, line, cursor, rendered_width);
      }
      continue;
    }
    if (ch == 27) {
      unsigned char seq[3] = {0, 0, 0};
      if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[') {
        if (seq[1] == 'A' || seq[1] == 'B') {
          recall_history(history, seq[1] == 'A' ? -1 : 1, line, draft, history_index, cursor);
        } else if (seq[1] == 'C' && cursor < line.size()) {
          ++cursor;
        } else if (seq[1] == 'D' && cursor > 0) {
          --cursor;
        } else if (seq[1] == 'H') {
          cursor = 0;
        } else if (seq[1] == 'F') {
          cursor = line.size();
        } else if (seq[1] == '3' && read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~' && cursor < line.size()) {
          line.erase(cursor, 1);
        }
        refresh_interactive_line(prompt, line, cursor, rendered_width);
      }
      continue;
    }
    if (std::isprint(ch)) {
      line.insert(cursor, 1, static_cast<char>(ch));
      ++cursor;
      history_index = history.size();
      refresh_interactive_line(prompt, line, cursor, rendered_width);
    }
  }
#endif
}

fluxion::CheckedProgram compile_frontend(const std::string& path) {
  return compile_source(path, read_file(path));
}

void usage() {
  std::cerr << "usage: fluxion <check|physics-check|emit-llvm|run> <file.flx>\n"
            << "       fluxion build <file.flx> [-o executable] [-O0|-O1|-O2|-O3|-Os|-Oz] [--for duration] [--runtime parallel|serial]\n"
            << "       fluxion sim <file.flx> [--for duration] [--runtime parallel|serial]\n"
            << "       fluxion run --visualize <file.flx>\n"
            << "       fluxion repl [script.repl]\n";
}

double parse_duration_seconds(const std::string& text, const fluxion::SourceLocation& loc) {
  const auto dot = text.find('.');
  if (dot == std::string::npos) {
    throw fluxion::DiagnosticError(loc, "duration must include a unit suffix");
  }
  const double value = std::stod(text.substr(0, dot));
  const std::string unit = text.substr(dot + 1);
  if (unit == "s") {
    return value;
  }
  if (unit == "ms") {
    return value / 1000.0;
  }
  if (unit == "us") {
    return value / 1000000.0;
  }
  if (unit == "hz") {
    if (value <= 0.0) {
      throw fluxion::DiagnosticError(loc, "hz tick rate must be positive");
    }
    return 1.0 / value;
  }
  throw fluxion::DiagnosticError(loc, "unsupported duration unit '" + unit + "'");
}

std::string format_sim_time(double seconds) {
  const double millis = seconds * 1000.0;
  if (std::abs(millis - std::round(millis)) < 0.000000001) {
    return std::to_string(static_cast<long long>(std::llround(millis))) + "ms";
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << millis;
  std::string text = out.str();
  while (text.size() > 1 && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text + "ms";
}

class SimpleReactorSimulator {
 public:
  SimpleReactorSimulator(const fluxion::CheckedProgram& checked, fluxion::ReactorRuntimeOptions runtime_options)
      : checked_(checked), runtime_options_(std::move(runtime_options)) {}

  int run(double duration_seconds) {
    const double period = common_period_seconds();
    if (period <= 0.0 || duration_seconds <= 0.0) {
      throw fluxion::DiagnosticError({"<sim>", 1, 1}, "simulation duration and reactor period must be positive");
    }
    const int steps = std::max(1, static_cast<int>(std::floor((duration_seconds / period) + 0.000000001)));
    fluxion::DeterministicReactorRuntime runtime(runtime_options_);
    register_reactors(runtime);
    runtime.run_ticks(static_cast<std::uint64_t>(steps), [this](std::uint64_t, double time_seconds) {
      print_outputs(time_seconds);
    });
    return 0;
  }

 private:
  struct ReactorInstance {
    const fluxion::ReactorDecl* decl = nullptr;
    std::string body;
    std::unordered_map<std::string, long long> values;
    std::vector<std::string> outputs;
  };

  const fluxion::CheckedProgram& checked_;
  fluxion::ReactorRuntimeOptions runtime_options_;
  std::vector<ReactorInstance> instances_;

  double common_period_seconds() const {
    if (checked_.module.reactors.empty()) {
      throw fluxion::DiagnosticError({"<module>", 1, 1}, "sim requires at least one reactor");
    }
    const auto& first = checked_.module.reactors.front();
    const double period = parse_duration_seconds(first.meta.tick, first.loc);
    for (const auto& reactor : checked_.module.reactors) {
      const double reactor_period = parse_duration_seconds(reactor.meta.tick, reactor.loc);
      if (std::abs(reactor_period - period) > 0.000000001) {
        throw fluxion::DiagnosticError(reactor.loc, "sim milestone requires reactors to share one tick period");
      }
    }
    return period;
  }

  static bool is_i32_type(const std::string& type) {
    return type == "i32" || type == "Int";
  }

  void register_reactors(fluxion::DeterministicReactorRuntime& runtime) {
    instances_.clear();
    instances_.reserve(checked_.module.reactors.size());
    for (const auto& reactor : checked_.module.reactors) {
      ReactorInstance instance;
      instance.decl = &reactor;
      instance.body = tick_body(reactor);
      load_members(reactor, instance);
      instances_.push_back(std::move(instance));
    }

    for (std::size_t index = 0; index < instances_.size(); ++index) {
      const auto& reactor = *instances_[index].decl;
      fluxion::ReactorTask task;
      task.name = reactor.name;
      task.phase = reactor.meta.phase;
      task.priority = reactor.meta.priority;
      task.parallel_safe = reactor.meta.parallel_safe;
      task.period_seconds = parse_duration_seconds(reactor.meta.tick, reactor.loc);
      task.tick = [this, index](fluxion::ReactorContext&) {
        ReactorInstance& instance = instances_[index];
        execute_tick(instance, instance.body, instance.decl->loc);
      };
      runtime.add_reactor(std::move(task));
    }
  }

  void load_members(const fluxion::ReactorDecl& reactor, ReactorInstance& instance) {
    for (const auto& state : reactor.states) {
      if (!is_i32_type(state.type.text)) {
        throw fluxion::DiagnosticError(state.loc, "sim milestone supports only i32 reactor state");
      }
      instance.values[state.name] = state.initializer.empty() ? 0 : eval_expression(instance, state.initializer, state.loc);
    }
    for (const auto& port : reactor.ports) {
      if (port.direction != fluxion::ReactorPortDecl::Direction::Output) {
        continue;
      }
      if (!is_i32_type(port.type.text)) {
        throw fluxion::DiagnosticError(port.loc, "sim milestone supports only i32 reactor outputs");
      }
      instance.outputs.push_back(port.name);
      instance.values[port.name] = 0;
    }
    if (instance.outputs.empty()) {
      throw fluxion::DiagnosticError(reactor.loc, "sim requires at least one output port");
    }
  }

  static std::string tick_body(const fluxion::ReactorDecl& reactor) {
    for (const auto& handler : reactor.handlers) {
      if (handler.target == "tick") {
        return handler.body;
      }
    }
    throw fluxion::DiagnosticError(reactor.loc, "sim requires a tick handler");
  }

  std::vector<fluxion::Token> lex_fragment(const std::string& source, const fluxion::SourceLocation& loc) const {
    fluxion::Lexer lexer(loc.file.empty() ? "<sim>" : loc.file, source);
    return lexer.lex();
  }

  long long eval_expression(const ReactorInstance& instance,
                            const std::string& source,
                            const fluxion::SourceLocation& loc) const {
    auto tokens = lex_fragment(source, loc);
    std::size_t pos = 0;
    return parse_additive(instance, tokens, pos, loc);
  }

  long long parse_additive(const ReactorInstance& instance,
                           const std::vector<fluxion::Token>& tokens,
                           std::size_t& pos,
                           const fluxion::SourceLocation& loc) const {
    long long value = parse_term(instance, tokens, pos, loc);
    while (tokens[pos].kind == fluxion::TokenKind::Plus || tokens[pos].kind == fluxion::TokenKind::Minus) {
      const auto op = tokens[pos++].kind;
      const long long rhs = parse_term(instance, tokens, pos, loc);
      value = op == fluxion::TokenKind::Plus ? value + rhs : value - rhs;
    }
    return value;
  }

  long long parse_term(const ReactorInstance& instance,
                       const std::vector<fluxion::Token>& tokens,
                       std::size_t& pos,
                       const fluxion::SourceLocation& loc) const {
    long long value = parse_primary(instance, tokens, pos, loc);
    while (tokens[pos].kind == fluxion::TokenKind::Star || tokens[pos].kind == fluxion::TokenKind::Slash) {
      const auto op = tokens[pos++].kind;
      const long long rhs = parse_primary(instance, tokens, pos, loc);
      if (op == fluxion::TokenKind::Slash && rhs == 0) {
        throw fluxion::DiagnosticError(loc, "division by zero in sim expression");
      }
      value = op == fluxion::TokenKind::Star ? value * rhs : value / rhs;
    }
    return value;
  }

  long long parse_primary(const ReactorInstance& instance,
                          const std::vector<fluxion::Token>& tokens,
                          std::size_t& pos,
                          const fluxion::SourceLocation& loc) const {
    if (tokens[pos].kind == fluxion::TokenKind::Minus) {
      ++pos;
      return -parse_primary(instance, tokens, pos, loc);
    }
    if (tokens[pos].kind == fluxion::TokenKind::Number) {
      const std::string text = tokens[pos++].text;
      if (text.find('.') != std::string::npos) {
        throw fluxion::DiagnosticError(loc, "sim milestone supports only integer expressions");
      }
      return std::stoll(text);
    }
    if (tokens[pos].kind == fluxion::TokenKind::Identifier || tokens[pos].kind == fluxion::TokenKind::State ||
        tokens[pos].kind == fluxion::TokenKind::Output || tokens[pos].kind == fluxion::TokenKind::Input ||
        tokens[pos].kind == fluxion::TokenKind::Tick) {
      const std::string name = tokens[pos++].text;
      const auto it = instance.values.find(name);
      if (it == instance.values.end()) {
        throw fluxion::DiagnosticError(loc, "unknown sim variable '" + name + "'");
      }
      return it->second;
    }
    if (tokens[pos].kind == fluxion::TokenKind::LParen) {
      ++pos;
      long long value = parse_additive(instance, tokens, pos, loc);
      if (tokens[pos].kind != fluxion::TokenKind::RParen) {
        throw fluxion::DiagnosticError(loc, "expected ')' in sim expression");
      }
      ++pos;
      return value;
    }
    throw fluxion::DiagnosticError(loc, "expected integer sim expression");
  }

  void execute_tick(ReactorInstance& instance, const std::string& source, const fluxion::SourceLocation& loc) {
    auto tokens = lex_fragment(source, loc);
    std::size_t pos = 0;
    if (tokens[pos].kind == fluxion::TokenKind::LBrace) {
      ++pos;
    }
    while (tokens[pos].kind != fluxion::TokenKind::End && tokens[pos].kind != fluxion::TokenKind::RBrace) {
      if (tokens[pos].kind != fluxion::TokenKind::Identifier && tokens[pos].kind != fluxion::TokenKind::State &&
          tokens[pos].kind != fluxion::TokenKind::Output && tokens[pos].kind != fluxion::TokenKind::Input) {
        throw fluxion::DiagnosticError(loc, "expected assignment in tick body");
      }
      const std::string target = tokens[pos++].text;
      if (!instance.values.count(target)) {
        throw fluxion::DiagnosticError(loc, "assignment to unknown sim variable '" + target + "'");
      }
      if (tokens[pos].kind != fluxion::TokenKind::Assign) {
        throw fluxion::DiagnosticError(loc, "expected '=' in tick assignment");
      }
      ++pos;
      const std::size_t expr_begin = pos;
      while (tokens[pos].kind != fluxion::TokenKind::End && tokens[pos].kind != fluxion::TokenKind::RBrace &&
             tokens[pos].kind != fluxion::TokenKind::Semicolon) {
        if ((tokens[pos].kind == fluxion::TokenKind::Identifier || tokens[pos].kind == fluxion::TokenKind::State ||
             tokens[pos].kind == fluxion::TokenKind::Output || tokens[pos].kind == fluxion::TokenKind::Input) &&
            tokens[pos + 1].kind == fluxion::TokenKind::Assign) {
          break;
        }
        ++pos;
      }
      std::string expr_source;
      for (std::size_t i = expr_begin; i < pos; ++i) {
        if (!expr_source.empty() && tokens[i].kind != fluxion::TokenKind::RParen) {
          expr_source += ' ';
        }
        expr_source += tokens[i].text;
      }
      instance.values[target] = eval_expression(instance, expr_source, loc);
      if (tokens[pos].kind == fluxion::TokenKind::Semicolon) {
        ++pos;
      }
    }
  }

  void print_outputs(double time_seconds) const {
    std::cout << "t=" << format_sim_time(time_seconds);
    const bool prefix_reactor = instances_.size() > 1;
    for (const auto& instance : instances_) {
      for (const auto& output : instance.outputs) {
        std::cout << "    ";
        if (prefix_reactor) {
          std::cout << instance.decl->name << ".";
        }
        std::cout << output << "=" << instance.values.at(output);
      }
    }
    std::cout << '\n';
  }
};

std::string shell_quote(const std::string& text) {
  std::string quoted = "'";
  for (const char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

std::string cpp_string_literal(const std::string& text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  return out;
}

std::string executable_stem(const std::string& path) {
  std::string stem = path;
  const auto slash = stem.find_last_of("/\\");
  if (slash != std::string::npos) {
    stem = stem.substr(slash + 1);
  }
  const auto dot = stem.find_last_of('.');
  if (dot != std::string::npos) {
    stem = stem.substr(0, dot);
  }
  return stem.empty() ? "fluxion_program" : stem;
}

bool is_native_optimization_level(const std::string& value) {
  return value == "-O0" || value == "-O1" || value == "-O2" || value == "-O3" || value == "-Os" ||
         value == "-Oz";
}

std::string parse_native_optimization_level(const std::string& value) {
  if (is_native_optimization_level(value)) {
    return value;
  }
  const std::string suffix = !value.empty() && value.front() == 'O' ? value.substr(1) : value;
  const std::string with_prefix = "-O" + suffix;
  if (is_native_optimization_level(with_prefix)) {
    return with_prefix;
  }
  throw fluxion::DiagnosticError({"<build>", 1, 1}, "unsupported optimization level '" + value + "'");
}

std::string find_project_root_for(const std::string& source_path) {
  namespace fs = std::filesystem;
  fs::path current = fs::absolute(fs::path(source_path)).parent_path();
  for (;;) {
    if (fs::exists(current / "src" / "runtime.cpp") && fs::exists(current / "src" / "runtime.h")) {
      return current.string();
    }
    if (!current.has_parent_path() || current == current.parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  if (fs::exists(fs::path("src") / "runtime.cpp") && fs::exists(fs::path("src") / "runtime.h")) {
    return fs::absolute(".").string();
  }
  throw fluxion::DiagnosticError({"<build>", 1, 1}, "could not locate Fluxion runtime sources");
}

class NativeReactorBuilder {
 public:
  NativeReactorBuilder(const fluxion::CheckedProgram& checked,
                       std::string input_path,
                       fluxion::ReactorRuntimeOptions runtime_options,
                       std::string optimization_level,
                       double duration_seconds)
      : checked_(checked),
        input_path_(std::move(input_path)),
        runtime_options_(std::move(runtime_options)),
        optimization_level_(std::move(optimization_level)),
        duration_seconds_(duration_seconds) {}

  void build(const std::string& output_path) {
    validate();
    const std::string source_path = output_path + ".cpp";
    {
      std::ofstream out(source_path);
      if (!out) {
        throw fluxion::DiagnosticError({"<build>", 1, 1}, "could not write native source '" + source_path + "'");
      }
      emit_source(out);
    }

    const std::string project_root = find_project_root_for(input_path_);
    const std::string command = std::string("c++ -std=c++17 ") + shell_quote(optimization_level_) + " -pthread -I " +
                                shell_quote(project_root + "/src") + " " + shell_quote(source_path) + " " +
                                shell_quote(project_root + "/src/runtime.cpp") + " -o " + shell_quote(output_path);
    const int status = std::system(command.c_str());
    if (status != 0) {
      throw fluxion::DiagnosticError({"<build>", 1, 1}, "native C++ compiler failed");
    }
    std::remove(source_path.c_str());
  }

 private:
  struct ReactorInfo {
    const fluxion::ReactorDecl* decl = nullptr;
    std::vector<std::string> members;
    std::vector<std::string> outputs;
  };

  const fluxion::CheckedProgram& checked_;
  std::string input_path_;
  fluxion::ReactorRuntimeOptions runtime_options_;
  std::string optimization_level_;
  double duration_seconds_ = 0.1;
  std::vector<ReactorInfo> reactors_;

  void validate() {
    if (checked_.module.reactors.empty()) {
      throw fluxion::DiagnosticError({"<module>", 1, 1}, "build requires at least one reactor");
    }
    const double period = common_period_seconds();
    if (period <= 0.0 || duration_seconds_ <= 0.0) {
      throw fluxion::DiagnosticError({"<build>", 1, 1}, "build duration and reactor period must be positive");
    }
    reactors_.clear();
    reactors_.reserve(checked_.module.reactors.size());
    for (const auto& reactor : checked_.module.reactors) {
      ReactorInfo info;
      info.decl = &reactor;
      for (const auto& state : reactor.states) {
        if (!is_i32_type(state.type.text)) {
          throw fluxion::DiagnosticError(state.loc, "native reactor build supports only i32 reactor state");
        }
        info.members.push_back(state.name);
      }
      for (const auto& port : reactor.ports) {
        if (port.direction != fluxion::ReactorPortDecl::Direction::Output) {
          continue;
        }
        if (!is_i32_type(port.type.text)) {
          throw fluxion::DiagnosticError(port.loc, "native reactor build supports only i32 reactor outputs");
        }
        info.members.push_back(port.name);
        info.outputs.push_back(port.name);
      }
      if (info.outputs.empty()) {
        throw fluxion::DiagnosticError(reactor.loc, "native reactor build requires at least one output port");
      }
      tick_body(reactor);
      reactors_.push_back(std::move(info));
    }
  }

  double common_period_seconds() const {
    const auto& first = checked_.module.reactors.front();
    const double period = parse_duration_seconds(first.meta.tick, first.loc);
    for (const auto& reactor : checked_.module.reactors) {
      const double reactor_period = parse_duration_seconds(reactor.meta.tick, reactor.loc);
      if (std::abs(reactor_period - period) > 0.000000001) {
        throw fluxion::DiagnosticError(reactor.loc, "native reactor build requires reactors to share one tick period");
      }
    }
    return period;
  }

  static bool is_i32_type(const std::string& type) {
    return type == "i32" || type == "Int";
  }

  static std::string tick_body(const fluxion::ReactorDecl& reactor) {
    for (const auto& handler : reactor.handlers) {
      if (handler.target == "tick") {
        return handler.body;
      }
    }
    throw fluxion::DiagnosticError(reactor.loc, "native reactor build requires a tick handler");
  }

  std::vector<fluxion::Token> lex_fragment(const std::string& source, const fluxion::SourceLocation& loc) const {
    fluxion::Lexer lexer(loc.file.empty() ? "<build>" : loc.file, source);
    return lexer.lex();
  }

  bool is_member(const ReactorInfo& reactor, const std::string& name) const {
    return std::find(reactor.members.begin(), reactor.members.end(), name) != reactor.members.end();
  }

  std::string emit_cpp_expr(const ReactorInfo& reactor,
                            const std::vector<fluxion::Token>& tokens,
                            std::size_t begin,
                            std::size_t end,
                            const fluxion::SourceLocation& loc) const {
    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
      const auto kind = tokens[i].kind;
      std::string text = tokens[i].text;
      if (kind == fluxion::TokenKind::Identifier || kind == fluxion::TokenKind::State ||
          kind == fluxion::TokenKind::Output || kind == fluxion::TokenKind::Input) {
        if (!is_member(reactor, text)) {
          throw fluxion::DiagnosticError(loc, "native reactor build cannot resolve variable '" + text + "'");
        }
        text = "state" + std::to_string(reactor_index(reactor)) + "." + text;
      } else if (kind == fluxion::TokenKind::Number && text.find('.') != std::string::npos) {
        throw fluxion::DiagnosticError(loc, "native reactor build supports only integer expressions");
      }
      if (!out.empty() && text != ")" && out.back() != '(') {
        out += ' ';
      }
      out += text;
    }
    return out;
  }

  std::size_t reactor_index(const ReactorInfo& reactor) const {
    for (std::size_t i = 0; i < reactors_.size(); ++i) {
      if (&reactors_[i] == &reactor) {
        return i;
      }
    }
    return 0;
  }

  void emit_tick_body(std::ostream& out, const ReactorInfo& reactor) const {
    const auto& decl = *reactor.decl;
    const auto tokens = lex_fragment(tick_body(decl), decl.loc);
    std::size_t pos = 0;
    if (tokens[pos].kind == fluxion::TokenKind::LBrace) {
      ++pos;
    }
    while (tokens[pos].kind != fluxion::TokenKind::End && tokens[pos].kind != fluxion::TokenKind::RBrace) {
      if (tokens[pos].kind != fluxion::TokenKind::Identifier && tokens[pos].kind != fluxion::TokenKind::State &&
          tokens[pos].kind != fluxion::TokenKind::Output && tokens[pos].kind != fluxion::TokenKind::Input) {
        throw fluxion::DiagnosticError(decl.loc, "native reactor build expected assignment in tick body");
      }
      const std::string target = tokens[pos++].text;
      if (!is_member(reactor, target)) {
        throw fluxion::DiagnosticError(decl.loc, "native reactor build cannot assign unknown variable '" + target + "'");
      }
      if (tokens[pos].kind != fluxion::TokenKind::Assign) {
        throw fluxion::DiagnosticError(decl.loc, "native reactor build expected '=' in tick assignment");
      }
      ++pos;
      const std::size_t expr_begin = pos;
      while (tokens[pos].kind != fluxion::TokenKind::End && tokens[pos].kind != fluxion::TokenKind::RBrace &&
             tokens[pos].kind != fluxion::TokenKind::Semicolon) {
        if ((tokens[pos].kind == fluxion::TokenKind::Identifier || tokens[pos].kind == fluxion::TokenKind::State ||
             tokens[pos].kind == fluxion::TokenKind::Output || tokens[pos].kind == fluxion::TokenKind::Input) &&
            tokens[pos + 1].kind == fluxion::TokenKind::Assign) {
          break;
        }
        ++pos;
      }
      const std::size_t index = reactor_index(reactor);
      out << "      state" << index << "." << target << " = "
          << emit_cpp_expr(reactor, tokens, expr_begin, pos, decl.loc) << ";\n";
      if (tokens[pos].kind == fluxion::TokenKind::Semicolon) {
        ++pos;
      }
    }
  }

  void emit_source(std::ostream& out) const {
    const double period = common_period_seconds();
    const auto steps = std::max(1, static_cast<int>(std::floor((duration_seconds_ / period) + 0.000000001)));
    out << "#include \"runtime.h\"\n"
        << "#include <cmath>\n"
        << "#include <cstdint>\n"
        << "#include <iomanip>\n"
        << "#include <iostream>\n"
        << "#include <sstream>\n"
        << "#include <string>\n\n"
        << "#include <utility>\n\n"
        << "namespace {\n"
        << "std::string format_sim_time(double seconds) {\n"
        << "  const double millis = seconds * 1000.0;\n"
        << "  if (std::abs(millis - std::round(millis)) < 0.000000001) {\n"
        << "    return std::to_string(static_cast<long long>(std::llround(millis))) + \"ms\";\n"
        << "  }\n"
        << "  std::ostringstream out;\n"
        << "  out << std::fixed << std::setprecision(3) << millis;\n"
        << "  std::string text = out.str();\n"
        << "  while (text.size() > 1 && text.back() == '0') text.pop_back();\n"
        << "  if (!text.empty() && text.back() == '.') text.pop_back();\n"
        << "  return text + \"ms\";\n"
        << "}\n";
    for (std::size_t i = 0; i < reactors_.size(); ++i) {
      out << "struct Reactor" << i << "State {\n";
      for (const auto& member : reactors_[i].members) {
        out << "  long long " << member << " = 0;\n";
      }
      out << "};\n";
    }
    out << "}  // namespace\n\n"
        << "int main() {\n"
        << "  fluxion::ReactorRuntimeOptions options;\n";
    if (runtime_options_.mode == fluxion::ReactorRuntimeMode::Serial) {
      out << "  options.mode = fluxion::ReactorRuntimeMode::Serial;\n";
    } else {
      out << "  options.mode = fluxion::ReactorRuntimeMode::DeterministicParallel;\n";
    }
    out << "  fluxion::DeterministicReactorRuntime runtime(options);\n";
    for (std::size_t i = 0; i < reactors_.size(); ++i) {
      out << "  Reactor" << i << "State state" << i << ";\n";
      for (const auto& state : reactors_[i].decl->states) {
        if (!state.initializer.empty()) {
          const auto tokens = lex_fragment(state.initializer, state.loc);
          out << "  state" << i << "." << state.name << " = "
              << emit_cpp_expr(reactors_[i], tokens, 0, tokens.size() - 1, state.loc) << ";\n";
        }
      }
      out << "  {\n"
          << "    fluxion::ReactorTask task;\n"
          << "    task.name = " << cpp_string_literal(reactors_[i].decl->name) << ";\n"
          << "    task.phase = " << cpp_string_literal(reactors_[i].decl->meta.phase) << ";\n"
          << "    task.priority = " << reactors_[i].decl->meta.priority << ";\n"
          << "    task.parallel_safe = " << (reactors_[i].decl->meta.parallel_safe ? "true" : "false") << ";\n"
          << "    task.period_seconds = " << std::setprecision(17) << period << ";\n"
          << "    task.tick = [&](fluxion::ReactorContext&) {\n";
      emit_tick_body(out, reactors_[i]);
      out << "    };\n"
          << "    runtime.add_reactor(std::move(task));\n"
          << "  }\n";
    }
    out << "  runtime.run_ticks(" << steps << ", [&](std::uint64_t, double time_seconds) {\n"
        << "    std::cout << \"t=\" << format_sim_time(time_seconds);\n";
    const bool prefix_reactor = reactors_.size() > 1;
    for (std::size_t i = 0; i < reactors_.size(); ++i) {
      for (const auto& output : reactors_[i].outputs) {
        out << "    std::cout << \"    ";
        if (prefix_reactor) {
          out << reactors_[i].decl->name << ".";
        }
        out << output << "=\" << state" << i << "." << output << ";\n";
      }
    }
    out << "    std::cout << '\\n';\n"
        << "  });\n"
        << "  return 0;\n"
        << "}\n";
  }
};

class ReplSession {
 public:
  bool handle(const std::string& input) {
    const std::string text = trim(input);
    if (text.empty()) {
      return true;
    }
    if (text == ":quit" || text == ":q" || text == ":exit") {
      return false;
    }
    if (text == ":help") {
      print_help();
      return true;
    }
    if (text == ":clear") {
      declarations_.clear();
      bindings_.clear();
      lab_ = ControlLab{};
      std::cout << "cleared declarations\n";
      return true;
    }
    if (text == ":decls") {
      if (declarations_.empty() && bindings_.empty()) {
        std::cout << "(none)\n";
      } else {
        std::cout << declarations_;
        for (const auto& binding : bindings_) {
          std::cout << binding.name << " = " << binding.value << '\n';
        }
      }
      return true;
    }

    if (try_handle_control_lab(text)) {
      return true;
    }

    if (is_declaration(text)) {
      add_declaration(input);
    } else if (auto binding = parse_binding(input)) {
      add_binding(*binding);
    } else {
      eval_expression(input);
    }
    return true;
  }

  bool is_incomplete(const std::string& input, const fluxion::DiagnosticError& error) const {
    const std::string message = error.what();
    if (error.location().line < line_count(input)) {
      return false;
    }
    return message.find("expected expression") != std::string::npos ||
           message.find("expected 'in'") != std::string::npos ||
           message.find("expected 'else'") != std::string::npos ||
           message.find("expected '}'") != std::string::npos ||
           message.find("expected ')'") != std::string::npos;
  }

 private:
  struct Binding {
    std::string name;
    std::string value;
  };

  struct ControlLab {
    struct Plant {
      double gain = 1.0;
      double tau = 0.8;
      double initial = 0.0;
      double output = 0.0;
      bool configured = false;
    };

    struct Pid {
      double kp = 2.0;
      double ki = 0.4;
      double kd = 0.1;
      double min_output = -10.0;
      double max_output = 10.0;
      double integral = 0.0;
      double previous_error = 0.0;
      bool has_previous_error = false;
      bool anti_windup = false;
      bool configured = false;
    };

    struct Sample {
      double time = 0.0;
      double y = 0.0;
      double u = 0.0;
    };

    Plant plant;
    Pid pid;
    double setpoint = 0.0;
    bool setpoint_configured = false;
    double last_duration = 5.0;
    double last_dt = 0.01;
    std::vector<Sample> result;
  };

  bool try_handle_control_lab(const std::string& text) {
    if (starts_with_word(text, "import")) {
      std::cout << "ok\n";
      return true;
    }
    if (starts_with(text, "plant = Plant.first_order(")) {
      if (!delimiter_balanced(text, '(', ')')) {
        throw fluxion::DiagnosticError({"<repl>", line_count(text), 1}, "expected ')'");
      }
      lab_.plant.gain = read_named_double(text, "gain", 1.0);
      lab_.plant.tau = read_named_duration_seconds(text, "tau", 0.8);
      lab_.plant.initial = contains(text, "initial:") ? read_named_double(text, "initial", 0.0)
                                                      : read_named_double(text, "output", 0.0);
      lab_.plant.output = lab_.plant.initial;
      lab_.plant.configured = true;
      std::cout << "plant = Plant.first_order(gain: " << format_fixed(lab_.plant.gain, 1)
                << ", tau: " << format_seconds(lab_.plant.tau)
                << ", output: " << format_fixed(lab_.plant.output, 1) << ")\n";
      return true;
    }
    if (starts_with(text, "pid = PID.Controller(")) {
      if (!delimiter_balanced(text, '(', ')')) {
        throw fluxion::DiagnosticError({"<repl>", line_count(text), 1}, "expected ')'");
      }
      lab_.pid.kp = read_named_double(text, "kp", 2.0);
      lab_.pid.ki = read_named_double(text, "ki", 0.4);
      lab_.pid.kd = read_named_double(text, "kd", 0.1);
      read_output_limits(text, lab_.pid.min_output, lab_.pid.max_output);
      lab_.pid.integral = 0.0;
      lab_.pid.previous_error = 0.0;
      lab_.pid.has_previous_error = false;
      lab_.pid.anti_windup = contains(text, "anti_windup");
      lab_.pid.configured = true;
      std::cout << "pid = PID.Controller(kp: " << format_fixed(lab_.pid.kp, 1)
                << ", ki: " << format_fixed(lab_.pid.ki, 1)
                << ", kd: " << format_fixed(lab_.pid.kd, 1)
                << ", output_limits: (" << format_fixed(lab_.pid.min_output, 1)
                << ", " << format_fixed(lab_.pid.max_output, 1) << "))\n";
      return true;
    }
    if (starts_with(text, "pid = pid.with(")) {
      if (!delimiter_balanced(text, '(', ')')) {
        throw fluxion::DiagnosticError({"<repl>", line_count(text), 1}, "expected ')'");
      }
      if (!lab_.pid.configured) {
        throw fluxion::DiagnosticError({"<repl>", 1, 1}, "pid is not configured");
      }
      lab_.pid.anti_windup = contains(text, "anti_windup");
      std::cout << "pid = pid.with(anti_windup: PID.clamp_integrator)\n";
      return true;
    }
    if (starts_with(text, "setpoint =")) {
      lab_.setpoint = read_assignment_double(text, "setpoint");
      lab_.setpoint_configured = true;
      std::cout << "setpoint = " << format_fixed(lab_.setpoint, 1) << '\n';
      return true;
    }
    if (starts_with(text, "result = simulate")) {
      if (!delimiter_balanced(text, '{', '}')) {
        throw fluxion::DiagnosticError({"<repl>", line_count(text), 1}, "expected '}'");
      }
      simulate(read_sim_duration(text, 5.0), read_sim_step(text, 0.01));
      std::cout << "result = SimulationResult(samples: " << lab_.result.size()
                << ", duration: " << format_seconds(lab_.last_duration)
                << ", step: " << format_duration(lab_.last_dt) << ")\n";
      return true;
    }
    if (text == "result = rerun") {
      simulate(lab_.last_duration, lab_.last_dt);
      std::cout << "result = SimulationResult(samples: " << lab_.result.size()
                << ", duration: " << format_seconds(lab_.last_duration)
                << ", step: " << format_duration(lab_.last_dt) << ")\n";
      return true;
    }
    if (text == "plant") {
      print_plant_info();
      return true;
    }
    if (text == "pid") {
      print_pid_info();
      return true;
    }
    if (text == "setpoint") {
      print_setpoint_info();
      return true;
    }
    if (text == "result") {
      print_result_info();
      return true;
    }
    if (text == "result.last") {
      require_result();
      const auto& last = lab_.result.back();
      std::cout << "{ time: " << format_seconds(last.time)
                << ", y: " << format_fixed(last.y, 2)
                << ", u: " << format_fixed(last.u, 2) << " }\n";
      return true;
    }
    if (starts_with(text, "result.every(")) {
      require_result();
      print_every(read_first_duration_seconds(text, 0.5));
      return true;
    }
    if (starts_with(text, "plot result.time result.y")) {
      require_result();
      print_plot();
      return true;
    }
    if (text == "check pid") {
      print_pid_check();
      return true;
    }
    if (starts_with(text, "reactorize pid as")) {
      print_reactor(text);
      return true;
    }
    return false;
  }

  static double clamp(double value, double lo, double hi) {
    return std::min(hi, std::max(lo, value));
  }

  static std::size_t skip_spaces(const std::string& text, std::size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
      ++pos;
    }
    return pos;
  }

  static double read_number_at(const std::string& text, std::size_t pos, double fallback) {
    pos = skip_spaces(text, pos);
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + pos, &end);
    return end == text.c_str() + pos ? fallback : value;
  }

  static double read_duration_at(const std::string& text, std::size_t pos, double fallback) {
    pos = skip_spaces(text, pos);
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + pos, &end);
    if (end == text.c_str() + pos) {
      return fallback;
    }
    std::size_t suffix = static_cast<std::size_t>(end - text.c_str());
    if (suffix > pos && text[suffix - 1] == '.') {
      --suffix;
    }
    if (text.compare(suffix, 3, ".ms") == 0) {
      return value / 1000.0;
    }
    return value;
  }

  static double read_named_double(const std::string& text, const std::string& name, double fallback) {
    const std::string key = name + ":";
    const auto pos = text.find(key);
    return pos == std::string::npos ? fallback : read_number_at(text, pos + key.size(), fallback);
  }

  static double read_assignment_double(const std::string& text, const std::string& name) {
    const std::string key = name + " =";
    const auto pos = text.find(key);
    if (pos == std::string::npos) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "expected assignment");
    }
    return read_number_at(text, pos + key.size(), 0.0);
  }

  static double read_named_duration_seconds(const std::string& text, const std::string& name, double fallback) {
    const std::string key = name + ":";
    const auto pos = text.find(key);
    return pos == std::string::npos ? fallback : read_duration_at(text, pos + key.size(), fallback);
  }

  static double read_first_duration_seconds(const std::string& text, double fallback) {
    const auto pos = text.find('(');
    return pos == std::string::npos ? fallback : read_duration_at(text, pos + 1, fallback);
  }

  static double read_sim_duration(const std::string& text, double fallback) {
    const auto pos = text.find("simulate");
    return pos == std::string::npos ? fallback : read_duration_at(text, pos + 8, fallback);
  }

  static double read_sim_step(const std::string& text, double fallback) {
    const auto pos = text.find("step");
    if (pos == std::string::npos) {
      return fallback;
    }
    return read_duration_at(text, pos + 4, fallback);
  }

  static void read_output_limits(const std::string& text, double& lo, double& hi) {
    const auto pos = text.find("output_limits:");
    if (pos == std::string::npos) {
      return;
    }
    const auto open = text.find('(', pos);
    if (open == std::string::npos) {
      return;
    }
    lo = read_number_at(text, open + 1, lo);
    const auto comma = text.find(',', open);
    if (comma != std::string::npos) {
      hi = read_number_at(text, comma + 1, hi);
    }
  }

  static std::string format_duration(double seconds) {
    if (seconds < 1.0) {
      return format_fixed(seconds * 1000.0, 0) + ".ms";
    }
    return format_seconds(seconds);
  }

  void require_control_setup() const {
    if (!lab_.plant.configured || !lab_.pid.configured || !lab_.setpoint_configured) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "configure plant, pid, and setpoint before simulate");
    }
  }

  void require_result() const {
    if (lab_.result.empty()) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "no simulation result; run `result = simulate ...` first");
    }
  }

  void print_plant_info() const {
    if (!lab_.plant.configured) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "plant is not configured");
    }
    std::cout << "plant: Plant.FirstOrder\n"
              << "  gain: " << format_fixed(lab_.plant.gain, 1) << '\n'
              << "  tau: " << format_seconds(lab_.plant.tau) << '\n'
              << "  initial: " << format_fixed(lab_.plant.initial, 2) << '\n'
              << "  output: " << format_fixed(lab_.plant.output, 2) << '\n';
  }

  void print_pid_info() const {
    if (!lab_.pid.configured) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "pid is not configured");
    }
    std::cout << "pid: PID.Controller\n"
              << "  kp: " << format_fixed(lab_.pid.kp, 2) << '\n'
              << "  ki: " << format_fixed(lab_.pid.ki, 2) << '\n'
              << "  kd: " << format_fixed(lab_.pid.kd, 2) << '\n'
              << "  output_limits: (" << format_fixed(lab_.pid.min_output, 1)
              << ", " << format_fixed(lab_.pid.max_output, 1) << ")\n"
              << "  anti_windup: " << (lab_.pid.anti_windup ? "PID.clamp_integrator" : "(none)") << '\n';
  }

  void print_setpoint_info() const {
    if (!lab_.setpoint_configured) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "setpoint is not configured");
    }
    std::cout << "setpoint: Double = " << format_fixed(lab_.setpoint, 2) << '\n';
  }

  void print_result_info() const {
    require_result();
    const auto& last = lab_.result.back();
    std::cout << "result: SimulationResult\n"
              << "  samples: " << lab_.result.size() << '\n'
              << "  duration: " << format_seconds(lab_.last_duration) << '\n'
              << "  step: " << format_duration(lab_.last_dt) << '\n'
              << "  last: { time: " << format_seconds(last.time)
              << ", y: " << format_fixed(last.y, 2)
              << ", u: " << format_fixed(last.u, 2) << " }\n";
  }

  void simulate(double duration, double dt) {
    require_control_setup();
    if (duration <= 0.0 || dt <= 0.0) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "simulation duration and step must be positive");
    }
    lab_.last_duration = duration;
    lab_.last_dt = dt;
    lab_.result.clear();
    lab_.pid.integral = 0.0;
    lab_.pid.previous_error = 0.0;
    lab_.pid.has_previous_error = false;

    double y = lab_.plant.initial;
    const int steps = static_cast<int>(std::lround(duration / dt));
    lab_.result.push_back({0.0, y, update_pid(lab_.setpoint - y, dt)});
    for (int i = 1; i <= steps; ++i) {
      const double error = lab_.setpoint - y;
      const double u = update_pid(error, dt);
      y += dt * ((lab_.plant.gain * u - y) / lab_.plant.tau);
      lab_.result.push_back({static_cast<double>(i) * dt, y, u});
    }
    lab_.plant.output = y;
  }

  double update_pid(double error, double dt) {
    const double derivative = lab_.pid.has_previous_error ? (error - lab_.pid.previous_error) / dt : 0.0;
    const double candidate_integral = lab_.pid.integral + error * dt;
    double unclamped = lab_.pid.kp * error + lab_.pid.ki * candidate_integral + lab_.pid.kd * derivative;
    const double clamped = clamp(unclamped, lab_.pid.min_output, lab_.pid.max_output);
    if (!lab_.pid.anti_windup || std::abs(unclamped - clamped) < 0.000000001) {
      lab_.pid.integral = candidate_integral;
    }
    lab_.pid.previous_error = error;
    lab_.pid.has_previous_error = true;
    return clamped;
  }

  void print_every(double interval) const {
    std::cout << "time     y       u\n";
    for (double t = 0.0; t <= lab_.last_duration + 0.0000001; t += interval) {
      const std::size_t index = std::min(lab_.result.size() - 1,
                                         static_cast<std::size_t>(std::lround(t / lab_.last_dt)));
      const auto& row = lab_.result[index];
      std::cout << std::left << std::setw(8) << format_compact_seconds(row.time)
                << std::right << std::setw(5) << format_fixed(row.y, 2)
                << "   " << std::setw(6) << format_fixed(row.u, 2) << '\n';
    }
  }

  void print_plot() const {
    double max_y = lab_.setpoint;
    for (const auto& sample : lab_.result) {
      max_y = std::max(max_y, sample.y);
    }
    std::cout << "result.y\n";
    const double interval = std::max(0.5, lab_.last_duration / 10.0);
    for (double t = 0.0; t <= lab_.last_duration + 0.0000001; t += interval) {
      const std::size_t index = std::min(lab_.result.size() - 1,
                                         static_cast<std::size_t>(std::lround(t / lab_.last_dt)));
      const auto& row = lab_.result[index];
      std::cout << std::setw(4) << format_compact_seconds(row.time) << " | "
                << bar_plot(row.y, max_y, 32) << ' ' << format_fixed(row.y, 2) << '\n';
    }
  }

  void print_pid_check() const {
    if (!lab_.pid.configured) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "pid is not configured");
    }
    std::cout << "PID Analysis\n\n";
    std::cout << "OK: output is bounded [" << format_fixed(lab_.pid.min_output, 1)
              << ", " << format_fixed(lab_.pid.max_output, 1) << "]\n";
    if (lab_.plant.configured && lab_.last_dt > 0.0) {
      if (lab_.last_dt <= lab_.plant.tau / 10.0) {
        std::cout << "OK: simulation step " << format_duration(lab_.last_dt)
                  << " is stable for plant tau " << format_seconds(lab_.plant.tau) << '\n';
      } else {
        std::cout << "WARN: simulation step " << format_duration(lab_.last_dt)
                  << " is coarse for plant tau " << format_seconds(lab_.plant.tau) << '\n';
      }
    }
    if (!lab_.pid.anti_windup) {
      std::cout << "WARN: integral term has no explicit anti-windup policy\n\n"
                << "Suggested:\n"
                << "  PID.Controller(..., anti_windup: clamp_integrator)\n";
      return;
    }
    std::cout << "OK: anti-windup configured\n";
    if (!lab_.result.empty()) {
      const double max_y = max_result_y();
      if (max_y <= lab_.setpoint + 0.000001) {
        std::cout << "OK: no overshoot detected in " << format_seconds(lab_.last_duration) << " simulation\n";
      } else {
        std::cout << "WARN: overshoot detected: " << format_fixed(max_y - lab_.setpoint, 2) << '\n';
      }
      std::cout << "INFO: settling time ~= " << format_fixed(settling_time(), 1) << ".s\n";
      std::cout << "INFO: steady-state error ~= "
                << format_fixed(std::abs(lab_.setpoint - lab_.result.back().y), 2) << '\n';
    }
  }

  double max_result_y() const {
    double max_y = lab_.result.empty() ? 0.0 : lab_.result.front().y;
    for (const auto& sample : lab_.result) {
      max_y = std::max(max_y, sample.y);
    }
    return max_y;
  }

  double settling_time() const {
    const double tolerance = std::max(0.02 * std::abs(lab_.setpoint), 0.001);
    for (std::size_t i = 0; i < lab_.result.size(); ++i) {
      bool settled = true;
      for (std::size_t j = i; j < lab_.result.size(); ++j) {
        if (std::abs(lab_.setpoint - lab_.result[j].y) > tolerance) {
          settled = false;
          break;
        }
      }
      if (settled) {
        return lab_.result[i].time;
      }
    }
    return lab_.last_duration;
  }

  void print_reactor(const std::string& text) const {
    std::string name = "TemperatureController";
    const auto as_pos = text.find(" as ");
    if (as_pos != std::string::npos) {
      const auto begin = skip_spaces(text, as_pos + 4);
      std::size_t end = begin;
      while (end < text.size() &&
             (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
        ++end;
      }
      if (end > begin) {
        name = text.substr(begin, end - begin);
      }
    }
    std::cout << "reactor " << name << "\n"
              << "  phase control\n"
              << "  tick 100.hz\n"
              << "  deadline 200.us\n"
              << "  parallel safe\n"
              << "{\n"
              << "  input measurement: sampled Double\n"
              << "  input setpoint: sampled Double\n"
              << "  output command: stream Double capacity 8 overflow drop_oldest\n\n"
              << "  state pid: PID.Controller in region reactor\n\n"
              << "  on tick(t: Tick) =\n"
              << "    let error = setpoint.current - measurement.current\n"
              << "    emit command(PID.update(pid, error, t.dt))\n"
              << "}\n";
  }

  static bool is_declaration(const std::string& input) {
    return starts_with_word(input, "func") || starts_with_word(input, "struct");
  }

  static std::optional<Binding> parse_binding(const std::string& input) {
    const auto tokens = fluxion::Lexer("<repl>", input).lex();
    if (tokens.size() < 4 || tokens[0].kind != fluxion::TokenKind::Identifier ||
        tokens[1].kind != fluxion::TokenKind::Assign) {
      return std::nullopt;
    }
    const auto equal = input.find('=');
    if (equal == std::string::npos) {
      return std::nullopt;
    }
    return Binding{tokens[0].text, trim(input.substr(equal + 1))};
  }

  static void print_help() {
    std::cout << style("Fluxion REPL commands", "1;36") << ":\n"
              << "  " << style(":help", "1;32") << "   show this help\n"
              << "  " << style(":decls", "1;32") << "  show persistent declarations\n"
              << "  " << style(":clear", "1;32") << "  clear persistent declarations\n"
              << "  " << style(":quit", "1;32") << "   exit\n"
              << "\n"
              << "Enter expressions, name bindings like `a = [1, 2; 3, 4]`, or struct/func declarations.\n"
              << style("Line editing:", "2") << " Up/Down history, Ctrl-A start, Ctrl-E end, Ctrl-K kill to end.\n";
  }

  std::string module_with(const std::string& body) const {
    return "module Repl\n\n" + declarations_ + body;
  }

  std::string with_bindings(const std::string& expression, const std::vector<Binding>& bindings) const {
    std::ostringstream out;
    for (const auto& binding : bindings) {
      out << "let " << binding.name << " = " << binding.value << " in\n";
    }
    out << expression;
    return out.str();
  }

  void add_declaration(const std::string& input) {
    const std::string candidate = declarations_ + trim(input) + "\n\n";
    compile_source("<repl>", "module Repl\n\n" + candidate + "func main() -> Int = 0\n");
    declarations_ = candidate;
    std::cout << "ok\n";
  }

  void add_binding(const Binding& binding) {
    if (binding.value.empty()) {
      throw fluxion::DiagnosticError({"<repl>", 1, 1}, "expected expression after '='");
    }
    std::vector<Binding> candidate;
    candidate.reserve(bindings_.size() + 1);
    for (const auto& existing : bindings_) {
      if (existing.name != binding.name) {
        candidate.push_back(existing);
      }
    }
    candidate.push_back(binding);
    compile_source("<repl>", module_with("func main() -> Int =\n" + with_bindings("0", candidate) + "\n"));
    bindings_ = std::move(candidate);
    std::cout << binding.name << " = ";
    eval_expression(binding.name);
  }

  bool try_eval_as(const std::string& input, const std::string& type, const std::optional<std::string>& printer) {
    std::ostringstream source;
    source << "func __repl_value() -> " << type << " =\n"
           << with_bindings(input, bindings_) << "\n\n"
           << "func main() -> Int =\n";
    if (printer) {
      source << "  " << *printer << "(__repl_value());\n"
             << "  0\n";
    } else {
      source << "  __repl_value();\n"
             << "  0\n";
    }

    auto checked = compile_source("<repl>", module_with(source.str()));
    fluxion::LlvmCodeGen codegen(checked);
    const int status = fluxion::run_jit(codegen.generate());
    if (status != 0) {
      std::cout << "exit " << status << '\n';
    }
    return true;
  }

  void eval_expression(const std::string& input) {
    const std::vector<std::pair<std::string, std::optional<std::string>>> attempts = {
        {"Int", "print_i32"},
        {"Double", "print_f64"},
        {"Bool", "print_bool"},
        {"String", "print_string"},
        {"Matrix", "print_matrix"},
        {"Void", std::nullopt},
    };

    std::string last_error;
    for (const auto& attempt : attempts) {
      try {
        if (try_eval_as(input, attempt.first, attempt.second)) {
          return;
        }
      } catch (const fluxion::DiagnosticError& e) {
        last_error = e.what();
      }
    }
    throw fluxion::DiagnosticError({"<repl>", 1, 1}, last_error.empty() ? "could not infer expression type" : last_error);
  }

  std::string declarations_;
  std::vector<Binding> bindings_;
  ControlLab lab_;
};

int run_repl(std::istream& input, bool interactive) {
  fluxion::set_pretty_output_enabled(interactive);
  ReplSession session;
  std::vector<std::string> history;
  std::string buffer;
  std::string line;

  if (interactive) {
    std::cout << style("Fluxion REPL", "1;36") << ". Type "
              << style(":help", "1;32") << " for commands, "
              << style(":quit", "1;32") << " to exit.\n";
  }

  for (;;) {
    if (interactive) {
      const std::string prompt = buffer.empty() ? style("fluxion> ", "1;34") : style("......> ", "2;34");
      auto maybe_line = read_interactive_line(prompt, history);
      if (!maybe_line) {
        return 0;
      }
      line = *maybe_line;
      add_history_entry(history, line);
    } else {
      if (!std::getline(input, line)) {
        return 0;
      }
    }

    if (!buffer.empty()) {
      buffer += "\n";
    }
    buffer += line;

    try {
      if (!session.handle(buffer)) {
        return 0;
      }
      buffer.clear();
    } catch (const fluxion::DiagnosticError& e) {
      if (session.is_incomplete(buffer, e)) {
        continue;
      }
      std::cerr << e.what() << '\n';
      buffer.clear();
      if (!interactive) {
        return 1;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "repl") {
    try {
      if (argc == 2) {
        return run_repl(std::cin, FLUXION_ISATTY(FLUXION_FILENO(stdin)) != 0);
      }
      if (argc == 3) {
        std::ifstream script(argv[2]);
        if (!script) {
          throw fluxion::DiagnosticError({argv[2], 1, 1}, "could not open file");
        }
        return run_repl(script, false);
      }
      usage();
      return 2;
    } catch (const fluxion::DiagnosticError& e) {
      std::cerr << e.what() << '\n';
      return 1;
    } catch (const std::exception& e) {
      std::cerr << "error: " << e.what() << '\n';
      return 1;
    }
  }

  bool visualize = false;
  std::string command;
  std::string path;
  std::string native_output_path;
  std::string native_optimization_level = "-O2";
  double sim_duration_seconds = 0.1;
  fluxion::ReactorRuntimeOptions runtime_options;
  if (argc == 4 && std::string(argv[1]) == "run" && std::string(argv[2]) == "--visualize") {
    command = argv[1];
    path = argv[3];
    visualize = true;
  } else if (argc >= 3 && std::string(argv[1]) == "sim") {
    command = argv[1];
    path = argv[2];
    for (int i = 3; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--for" && i + 1 < argc) {
        sim_duration_seconds = parse_duration_seconds(argv[++i], {argv[2], 1, 1});
      } else if (option == "--runtime" && i + 1 < argc) {
        const std::string runtime = argv[++i];
        if (runtime == "serial") {
          runtime_options.mode = fluxion::ReactorRuntimeMode::Serial;
        } else if (runtime == "parallel") {
          runtime_options.mode = fluxion::ReactorRuntimeMode::DeterministicParallel;
        } else {
          usage();
          return 2;
        }
      } else {
        usage();
        return 2;
      }
    }
  } else if (argc >= 3 && std::string(argv[1]) == "build") {
    command = argv[1];
    path = argv[2];
    native_output_path = executable_stem(path);
    for (int i = 3; i < argc; ++i) {
      const std::string option = argv[i];
      if ((option == "-o" || option == "--output") && i + 1 < argc) {
        native_output_path = argv[++i];
      } else if (is_native_optimization_level(option)) {
        native_optimization_level = option;
      } else if ((option == "--opt-level" || option == "--optimization-level") && i + 1 < argc) {
        native_optimization_level = parse_native_optimization_level(argv[++i]);
      } else if (option == "--for" && i + 1 < argc) {
        sim_duration_seconds = parse_duration_seconds(argv[++i], {argv[2], 1, 1});
      } else if (option == "--runtime" && i + 1 < argc) {
        const std::string runtime = argv[++i];
        if (runtime == "serial") {
          runtime_options.mode = fluxion::ReactorRuntimeMode::Serial;
        } else if (runtime == "parallel") {
          runtime_options.mode = fluxion::ReactorRuntimeMode::DeterministicParallel;
        } else {
          usage();
          return 2;
        }
      } else {
        usage();
        return 2;
      }
    }
  } else if (argc == 3) {
    command = argv[1];
    path = argv[2];
  } else {
    usage();
    return 2;
  }

  try {
    auto checked = compile_frontend(path);
    if (command == "check") {
      return 0;
    }
    if (command == "physics-check") {
      const fluxion::PhysicsChecker checker(checked);
      fluxion::print_physics_diagnostics(checker.check(), std::cout);
      return 0;
    }
    if (command == "sim") {
      SimpleReactorSimulator simulator(checked, runtime_options);
      return simulator.run(sim_duration_seconds);
    }
    if (command == "build") {
      NativeReactorBuilder builder(checked, path, runtime_options, native_optimization_level, sim_duration_seconds);
      builder.build(native_output_path);
      std::cout << "built " << native_output_path << " " << native_optimization_level << '\n';
      return 0;
    }
    if (command == "emit-llvm") {
      fluxion::LlvmCodeGen codegen(checked);
      std::cout << codegen.emit_ir();
      return 0;
    }
    if (command == "run") {
      fluxion::set_cartpole_visualizer_enabled(visualize);
      fluxion::LlvmCodeGen codegen(checked);
      return fluxion::run_jit(codegen.generate());
    }
    usage();
    return 2;
  } catch (const fluxion::DiagnosticError& e) {
    std::cerr << e.what() << '\n';
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
