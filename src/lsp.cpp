#include "diagnostic.h"
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace {

struct Document {
  std::string uri;
  std::string text;
};

std::map<std::string, Document> documents;

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u00";
          constexpr char hex[] = "0123456789abcdef";
          out.push_back(hex[(c >> 4) & 0xf]);
          out.push_back(hex[c & 0xf]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

std::string json_string(const std::string& text) {
  return "\"" + json_escape(text) + "\"";
}

void write_message(const std::string& payload) {
  std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
  std::cout.flush();
}

std::optional<std::size_t> find_string_value_start(const std::string& json, const std::string& key) {
  const std::string quoted_key = "\"" + key + "\"";
  const std::size_t key_pos = json.find(quoted_key);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t colon = json.find(':', key_pos + quoted_key.size());
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  std::size_t pos = colon + 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return std::nullopt;
  }
  return pos + 1;
}

std::optional<std::string> parse_json_string_at(const std::string& json, std::size_t pos) {
  std::string out;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '"') {
      return out;
    }
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (pos >= json.size()) {
      return std::nullopt;
    }
    const char escaped = json[pos++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        out.push_back(escaped);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u':
        if (pos + 4 > json.size()) {
          return std::nullopt;
        }
        // Fluxion sources and paths are ASCII in this repository; preserve
        // non-ASCII escapes as '?' rather than adding a full JSON decoder.
        out.push_back('?');
        pos += 4;
        break;
      default:
        return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::string> string_field(const std::string& json, const std::string& key) {
  const auto start = find_string_value_start(json, key);
  if (!start) {
    return std::nullopt;
  }
  return parse_json_string_at(json, *start);
}

std::optional<std::string> id_field(const std::string& json) {
  const std::string key = "\"id\"";
  const std::size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  std::size_t pos = colon + 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos < json.size() && json[pos] == '"') {
    auto id = parse_json_string_at(json, pos + 1);
    if (!id) {
      return std::nullopt;
    }
    return json_string(*id);
  }
  const std::size_t end = json.find_first_of(",}\r\n \t", pos);
  if (end == std::string::npos || end == pos) {
    return std::nullopt;
  }
  return json.substr(pos, end - pos);
}

std::string uri_to_path(const std::string& uri) {
  constexpr const char* prefix = "file://";
  if (uri.compare(0, 7, prefix) != 0) {
    return uri;
  }
  std::string path = uri.substr(7);
#ifdef _WIN32
  if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
    path.erase(path.begin());
  }
#endif
  std::string decoded;
  decoded.reserve(path.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      const std::string hex = path.substr(i + 1, 2);
      char* end = nullptr;
      const long value = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        decoded.push_back(static_cast<char>(value));
        i += 2;
        continue;
      }
    }
    decoded.push_back(path[i]);
  }
  return decoded;
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

std::string diagnostic_payload(const std::string& uri, const fluxion::DiagnosticError& error) {
  const auto& loc = error.location();
  const int line = std::max(0, loc.line - 1);
  const int column = std::max(0, loc.column - 1);
  std::string message = error.what();
  const std::string prefix = loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": ";
  if (message.compare(0, prefix.size(), prefix) == 0) {
    message = message.substr(prefix.size());
  }
  return "{\"uri\":" + json_string(uri) +
         ",\"diagnostics\":[{\"range\":{\"start\":{\"line\":" + std::to_string(line) +
         ",\"character\":" + std::to_string(column) + "},\"end\":{\"line\":" + std::to_string(line) +
         ",\"character\":" + std::to_string(column + 1) +
         "}},\"severity\":1,\"source\":\"fluxion\",\"message\":" + json_string(message) + "}]}";
}

void publish_diagnostics(const std::string& uri, const std::string& text) {
  const std::string path = uri_to_path(uri);
  std::string params;
  try {
    fluxion::Lexer lexer(path, text);
    fluxion::Parser parser(lexer.lex());
    fluxion::TypeChecker checker(parser.parse_module());
    checker.check();
    params = "{\"uri\":" + json_string(uri) + ",\"diagnostics\":[]}";
  } catch (const fluxion::DiagnosticError& error) {
    params = diagnostic_payload(uri, error);
  } catch (const std::exception& error) {
    params = "{\"uri\":" + json_string(uri) +
             ",\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
             "\"severity\":1,\"source\":\"fluxion\",\"message\":" +
             json_string(error.what()) + "}]}";
  }
  write_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":" + params + "}");
}

void respond(const std::optional<std::string>& id, const std::string& result) {
  if (!id) {
    return;
  }
  write_message("{\"jsonrpc\":\"2.0\",\"id\":" + *id + ",\"result\":" + result + "}");
}

void handle_message(const std::string& message) {
  const auto method = string_field(message, "method");
  if (!method) {
    return;
  }
  const auto id = id_field(message);
  if (*method == "initialize") {
    respond(id,
            "{\"capabilities\":{\"textDocumentSync\":1},\"serverInfo\":{\"name\":\"fluxion-lsp\",\"version\":\"0.1\"}}");
  } else if (*method == "shutdown") {
    respond(id, "null");
  } else if (*method == "textDocument/didOpen" || *method == "textDocument/didChange") {
    const auto uri = string_field(message, "uri");
    const auto text = string_field(message, "text");
    if (uri && text) {
      documents[*uri] = Document{*uri, *text};
      publish_diagnostics(*uri, *text);
    }
  } else if (*method == "textDocument/didSave") {
    const auto uri = string_field(message, "uri");
    if (!uri) {
      return;
    }
    const auto found = documents.find(*uri);
    if (found != documents.end()) {
      publish_diagnostics(*uri, found->second.text);
      return;
    }
    const auto text = read_file(uri_to_path(*uri));
    if (text) {
      publish_diagnostics(*uri, *text);
    }
  } else if (*method == "textDocument/diagnostic") {
    const auto uri = string_field(message, "uri");
    if (uri) {
      const auto found = documents.find(*uri);
      if (found != documents.end()) {
        publish_diagnostics(*uri, found->second.text);
      }
    }
    respond(id, "{\"kind\":\"full\",\"items\":[]}");
  }
}

}  // namespace

int main() {
  std::string header;
  while (std::getline(std::cin, header)) {
    if (!header.empty() && header.back() == '\r') {
      header.pop_back();
    }
    if (header.empty()) {
      continue;
    }
    constexpr const char* content_length = "Content-Length:";
    if (header.compare(0, 15, content_length) != 0) {
      continue;
    }
    const std::size_t length = static_cast<std::size_t>(std::stoul(header.substr(15)));
    while (std::getline(std::cin, header)) {
      if (!header.empty() && header.back() == '\r') {
        header.pop_back();
      }
      if (header.empty()) {
        break;
      }
    }
    std::string message(length, '\0');
    std::cin.read(&message[0], static_cast<std::streamsize>(length));
    if (!std::cin) {
      return 0;
    }
    handle_message(message);
  }
  return 0;
}
