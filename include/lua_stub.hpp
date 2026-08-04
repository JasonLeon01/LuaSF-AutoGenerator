#pragma once

#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lua_sf::stub {

inline std::ofstream &output() {
  static std::ofstream stream;
  return stream;
}

inline std::string &pending_class() {
  static std::string name;
  return name;
}

inline std::string &pending_doc() {
  static std::string text;
  return text;
}

inline std::set<std::string> &declared_tables() {
  static std::set<std::string> tables;
  return tables;
}

struct PendingFunction {
  std::string owner;
  std::string name;
  std::string type;
  std::string doc;
  std::vector<std::string> overloads;
};

struct FunctionParameter {
  std::string name;
  std::string type;
};

struct FunctionType {
  std::vector<FunctionParameter> parameters;
  std::vector<std::string> returns;
  bool valid = false;
};

inline PendingFunction &pending_function() {
  static PendingFunction pf;
  return pf;
}

inline bool enabled() { return output().is_open(); }

inline void declare_table(const std::string &name) {
  if (!enabled() || declared_tables().contains(name))
    return;

  const auto dot = name.rfind('.');
  if (dot != std::string::npos)
    declare_table(name.substr(0, dot));

  output() << name << " = " << name << " or {}\n";
  declared_tables().insert(name);
}

inline void close_pending_class();

inline bool is_doc_command_char(char value) {
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z');
}

inline bool is_doc_path_prefix(char value) {
  return is_doc_command_char(value) ||
         (value >= '0' && value <= '9') || value == '_' || value == ':' ||
         value == '/' || value == '\\';
}

inline bool is_doxygen_command(std::string_view value) {
  static constexpr std::array<std::string_view, 21> commands = {
      "a",          "b",       "brief",   "c",       "code",
      "deprecated", "e",       "em",      "endcode", "ingroup",
      "li",         "note",    "overload", "p",       "param",
      "relates",    "return",  "see",     "throws",  "warning"};
  for (const std::string_view command : commands) {
    if (value == command)
      return true;
  }
  return false;
}

inline std::string normalize_doc_line(const std::string &line) {
  std::string result;
  result.reserve(line.size());
  for (std::size_t index = 0; index < line.size();) {
    if (line[index] != '\\' ||
        (index > 0 && is_doc_path_prefix(line[index - 1])) ||
        index + 1 >= line.size() ||
        !is_doc_command_char(line[index + 1])) {
      result.push_back(line[index]);
      ++index;
      continue;
    }
    std::size_t end = index + 1;
    while (end < line.size() && is_doc_command_char(line[end]))
      ++end;
    const std::string_view command(line.data() + index + 1, end - index - 1);
    result.push_back(is_doxygen_command(command) ? '@' : '\\');
    result.append(command);
    index = end;
  }
  return result;
}

inline void write_doc(const std::string &text) {
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string normalized = normalize_doc_line(line);
    output() << "---";
    if (!normalized.empty())
      output() << " " << normalized;
    output() << "\n";
  }
}

inline void write_pending_doc() {
  auto &text = pending_doc();
  if (text.empty())
    return;

  const std::string current = std::move(text);
  text.clear();
  write_doc(current);
}

inline std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

inline std::vector<std::string> split_top_level(const std::string &value) {
  std::vector<std::string> result;
  std::size_t start = 0;
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  int angles = 0;
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    if (quote != '\0') {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == quote) {
        quote = '\0';
      }
      continue;
    }
    if (current == '\'' || current == '"') {
      quote = current;
    } else if (current == '(') {
      ++parentheses;
    } else if (current == ')') {
      --parentheses;
    } else if (current == '[') {
      ++brackets;
    } else if (current == ']') {
      --brackets;
    } else if (current == '{') {
      ++braces;
    } else if (current == '}') {
      --braces;
    } else if (current == '<') {
      ++angles;
    } else if (current == '>') {
      --angles;
    } else if (current == ',' && parentheses == 0 && brackets == 0 &&
               braces == 0 && angles == 0) {
      result.push_back(trim(value.substr(start, index - start)));
      start = index + 1;
    }
  }
  const std::string tail = trim(value.substr(start));
  if (!tail.empty())
    result.push_back(tail);
  return result;
}

inline std::size_t find_top_level_colon(const std::string &value) {
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  int angles = 0;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    if (current == '(')
      ++parentheses;
    else if (current == ')')
      --parentheses;
    else if (current == '[')
      ++brackets;
    else if (current == ']')
      --brackets;
    else if (current == '{')
      ++braces;
    else if (current == '}')
      --braces;
    else if (current == '<')
      ++angles;
    else if (current == '>')
      --angles;
    else if (current == ':' && parentheses == 0 && brackets == 0 &&
             braces == 0 && angles == 0)
      return index;
  }
  return std::string::npos;
}

inline FunctionType parse_function_type(const std::string &value) {
  FunctionType result;
  const std::string type = trim(value);
  if (!type.starts_with("fun("))
    return result;

  int depth = 0;
  std::size_t close = std::string::npos;
  for (std::size_t index = 3; index < type.size(); ++index) {
    if (type[index] == '(') {
      ++depth;
    } else if (type[index] == ')') {
      --depth;
      if (depth == 0) {
        close = index;
        break;
      }
    }
  }
  if (close == std::string::npos)
    return result;

  for (const std::string &parameter :
       split_top_level(type.substr(4, close - 4))) {
    const auto colon = find_top_level_colon(parameter);
    if (colon == std::string::npos)
      return result;
    std::string name = trim(parameter.substr(0, colon));
    if (name.ends_with('?'))
      name.pop_back();
    const std::string parameter_type = trim(parameter.substr(colon + 1));
    if (name.empty() || parameter_type.empty())
      return result;
    result.parameters.push_back({std::move(name), parameter_type});
  }

  std::string return_text = trim(type.substr(close + 1));
  if (!return_text.empty()) {
    if (!return_text.starts_with(':'))
      return result;
    result.returns = split_top_level(return_text.substr(1));
  }
  result.valid = true;
  return result;
}

inline void flush_pending_function() {
  auto &pf = pending_function();
  if (!enabled() || pf.name.empty())
    return;

  // Capture and clear before any call that may re-enter flush_pending_function.
  const PendingFunction current = std::move(pf);
  pf = PendingFunction{};

  close_pending_class();
  declare_table(current.owner);
  write_doc(current.doc);
  if (!current.overloads.empty()) {
    const FunctionType function_type = parse_function_type(current.type);
    if (function_type.valid) {
      for (const auto &overload : current.overloads)
        output() << "---@overload " << overload << "\n";
      for (const auto &parameter : function_type.parameters) {
        if (parameter.name == "...")
          output() << "---@vararg " << parameter.type << "\n";
        else
          output() << "---@param " << parameter.name << " " << parameter.type
                   << "\n";
      }
      for (const auto &return_type : function_type.returns)
        output() << "---@return " << return_type << "\n";
      output() << "function " << current.owner << "." << current.name << "(";
      for (std::size_t index = 0; index < function_type.parameters.size();
           ++index) {
        if (index != 0)
          output() << ", ";
        output() << function_type.parameters[index].name;
      }
      output() << ") end\n";
      return;
    }
  }
  output() << "---@type " << current.type << "\n";
  for (const auto &ol : current.overloads)
    output() << "---@overload " << ol << "\n";
  output() << current.owner << "." << current.name << " = function() end\n";
}

inline void close_pending_class() {
  flush_pending_function();
  if (!enabled() || pending_class().empty())
    return;

  declare_table(pending_class());
  pending_class().clear();
}

inline bool begin(const char *path) {
  output().open(path, std::ios::binary);
  if (!enabled())
    return false;

  declared_tables().clear();
  pending_doc().clear();
  output() << "---@meta\n\n";
  output() << "sf = sf or {}\n";
  declared_tables().insert("sf");
  return true;
}

inline void end() {
  flush_pending_function();
  close_pending_class();
  declared_tables().clear();
  pending_doc().clear();
  if (enabled())
    output().close();
}

inline void class_(const char *name, const char *bases = "") {
  if (!enabled())
    return;

  flush_pending_function();
  close_pending_class();
  const bool documented = !pending_doc().empty();
  write_pending_doc();
  pending_class() = name;
  output() << (documented ? "---@class " : "\n---@class ") << name;
  if (bases != nullptr && bases[0] != '\0')
    output() << " : " << bases;
  output() << "\n";
}

inline void alias_(const char *name, const char *target) {
  if (!enabled())
    return;

  flush_pending_function();
  close_pending_class();
  write_pending_doc();
  output() << "---@alias " << name << " " << target << "\n";
}

inline void doc(const char *text) {
  if (!enabled() || text == nullptr || text[0] == '\0')
    return;

  // A docstring belongs to the declaration emitted by the following stub macro.
  // Flush a preceding function first, but retain a pending class so this can
  // also annotate its fields.
  flush_pending_function();
  pending_doc() = text;
}

inline void field(const char *name, const char *type) {
  if (!enabled())
    return;

  if (!pending_class().empty()) {
    write_pending_doc();
    output() << "---@field " << name << " " << type << "\n";
    return;
  }

  flush_pending_function();
  write_pending_doc();
  output() << "---@type " << type << "\n";
  output() << name << " = nil\n";
}

inline void value(const char *owner, const char *name, const char *type) {
  if (!enabled())
    return;

  flush_pending_function();
  close_pending_class();
  declare_table(owner);
  write_pending_doc();
  output() << "---@type " << type << "\n";
  output() << owner << "." << name << " = nil\n";
}

inline void function(const char *owner, const char *name, const char *type) {
  if (!enabled())
    return;

  flush_pending_function();
  pending_function() = {owner, name, type, std::move(pending_doc()), {}};
  pending_doc().clear();
}

inline void overload(const char *owner, const char *name, const char *type) {
  if (!enabled())
    return;

  auto &pf = pending_function();
  if (pf.owner == owner && pf.name == name)
    pf.overloads.push_back(type);
}

inline void operator_(const char *owner, const char *annotation) {
  if (!enabled())
    return;

  flush_pending_function();
  close_pending_class();
  const bool documented = !pending_doc().empty();
  write_pending_doc();
  output() << (documented ? "---@class " : "\n---@class ") << owner << "\n";
  output() << "---@operator " << annotation << "\n";
  declare_table(owner);
}

inline void indexed_field(const char *owner, const char *key_type,
                          const char *value_type) {
  if (!enabled())
    return;

  flush_pending_function();
  close_pending_class();
  const bool documented = !pending_doc().empty();
  write_pending_doc();
  output() << (documented ? "---@class " : "\n---@class ") << owner << "\n";
  output() << "---@field [" << key_type << "] " << value_type << "\n";
  declare_table(owner);
}

} // namespace lua_sf::stub

#define LUASF_STUB_CLASS(...) ::lua_sf::stub::class_(__VA_ARGS__)
#define LUASF_STUB_ALIAS(name, target) ::lua_sf::stub::alias_(name, target)
#define LUASF_STUB_DOC(text) ::lua_sf::stub::doc(text)
#define LUASF_STUB_FIELD(name, type_str) ::lua_sf::stub::field(name, type_str)
#define LUASF_STUB_VALUE(owner, name, type_str)                                \
  ::lua_sf::stub::value(owner, name, type_str)
#define LUASF_STUB_FUNCTION(owner, name, type_str)                             \
  ::lua_sf::stub::function(owner, name, type_str)
#define LUASF_STUB_OVERLOAD(owner, name, type_str)                             \
  ::lua_sf::stub::overload(owner, name, type_str)
#define LUASF_STUB_OPERATOR(owner, annotation)                                 \
  ::lua_sf::stub::operator_(owner, annotation)
#define LUASF_STUB_INDEX_FIELD(owner, key_type, value_type)                    \
  ::lua_sf::stub::indexed_field(owner, key_type, value_type)
