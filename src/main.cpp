#include "codegen_llvm.h"
#include "diagnostic.h"
#include "jit.h"
#include "lexer.h"
#include "parser.h"
#include "physics_check.h"
#include "runtime.h"
#include "typecheck.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
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
            << "       fluxion run --visualize <file.flx>\n"
            << "       fluxion repl [script.repl]\n";
}

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
  if (argc == 4 && std::string(argv[1]) == "run" && std::string(argv[2]) == "--visualize") {
    command = argv[1];
    path = argv[3];
    visualize = true;
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
