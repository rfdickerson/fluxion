#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace fluxion {

struct SourceLocation {
  std::string file;
  int line = 1;
  int column = 1;
};

class DiagnosticError : public std::runtime_error {
 public:
  DiagnosticError(SourceLocation loc, const std::string& message)
      : std::runtime_error(format(loc, message)), loc_(std::move(loc)) {}

  const SourceLocation& location() const { return loc_; }

 private:
  static std::string format(const SourceLocation& loc, const std::string& message) {
    return loc.file + ":" + std::to_string(loc.line) + ":" +
           std::to_string(loc.column) + ": " + message;
  }

  SourceLocation loc_;
};

}  // namespace fluxion
