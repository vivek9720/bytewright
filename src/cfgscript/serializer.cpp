// bytewright - cfgscript canonical serializer implementation.
//
// The renderer walks the evaluated model (Document -> Node -> Value) and emits
// canonical source. It owns its own string escaper rather than reusing
// common::escape, because the canonical form must round-trip through the lexer:
// it emits the short escapes the lexer understands (\n \t \r \" \\) and falls
// back to \xHH for the remaining control and non-ASCII bytes.
#include "cfgscript/serializer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

namespace {

const char* kHexDigits = "0123456789abcdef";

void append_hex_byte(std::string& out, unsigned char c) {
  out += "\\x";
  out.push_back(kHexDigits[(c >> 4) & 0xF]);
  out.push_back(kHexDigits[c & 0xF]);
}

void append_indent(std::string& out, int depth, int width) {
  for (int level = 0; level < depth; ++level) {
    for (int k = 0; k < width; ++k) out.push_back(' ');
  }
}

// A double formatted by std::to_string can carry trailing zeros; the
// evaluator's own pretty printer uses std::to_string too, so we match it for
// stability rather than inventing a shorter representation.
void append_double(std::string& out, double value) {
  out += std::to_string(value);
}

void append_quoted(std::string& out, const std::string& s) {
  out.push_back('"');
  out += escape_string(s);
  out.push_back('"');
}

// Forward declaration: values are mutually recursive with arrays/objects.
void render_value(std::string& out, const Value& v, const SerializeOptions& opts,
                  int depth);

// Decide whether an aggregate of `count` elements renders inline. depth==0 keeps
// things simple by always allowing inline for small aggregates.
bool render_inline(std::size_t count, const SerializeOptions& opts) {
  return count <= opts.inline_threshold;
}

void render_array(std::string& out, const ValueArray& arr,
                  const SerializeOptions& opts, int depth) {
  if (arr.empty()) {
    out += "[]";
    return;
  }
  if (render_inline(arr.size(), opts)) {
    out.push_back('[');
    for (std::size_t k = 0; k < arr.size(); ++k) {
      if (k != 0) out += ", ";
      render_value(out, arr[k], opts, depth);
    }
    out.push_back(']');
    return;
  }
  out += "[\n";
  for (std::size_t k = 0; k < arr.size(); ++k) {
    append_indent(out, depth + 1, opts.indent_width);
    render_value(out, arr[k], opts, depth + 1);
    out += ",\n";
  }
  append_indent(out, depth, opts.indent_width);
  out.push_back(']');
}

void render_object(std::string& out, const ValueObject& obj,
                   const SerializeOptions& opts, int depth) {
  if (obj.empty()) {
    out += "{}";
    return;
  }
  if (render_inline(obj.size(), opts)) {
    out += "{ ";
    for (std::size_t k = 0; k < obj.size(); ++k) {
      if (k != 0) out += " ";
      out += obj[k].first;
      out += " = ";
      render_value(out, obj[k].second, opts, depth);
      out.push_back(';');
    }
    out += " }";
    return;
  }
  out += "{\n";
  for (std::size_t k = 0; k < obj.size(); ++k) {
    append_indent(out, depth + 1, opts.indent_width);
    out += obj[k].first;
    out += " = ";
    render_value(out, obj[k].second, opts, depth + 1);
    out += ";\n";
  }
  append_indent(out, depth, opts.indent_width);
  out.push_back('}');
}

void render_value(std::string& out, const Value& v, const SerializeOptions& opts,
                  int depth) {
  switch (v.type) {
    case ValueType::kNull:
      out += "null";
      break;
    case ValueType::kBool:
      out += v.b ? "true" : "false";
      break;
    case ValueType::kInt:
      out += std::to_string(v.i);
      break;
    case ValueType::kFloat:
      append_double(out, v.f);
      break;
    case ValueType::kStr:
      append_quoted(out, v.s);
      break;
    case ValueType::kArray:
      render_array(out, v.array, opts, depth);
      break;
    case ValueType::kObject:
      render_object(out, v.object, opts, depth);
      break;
  }
}

void render_path(std::string& out, const std::vector<std::string>& path) {
  for (std::size_t k = 0; k < path.size(); ++k) {
    if (k != 0) out.push_back('.');
    out += path[k];
  }
}

void render_nodes(std::string& out, const std::vector<Node>& nodes,
                  const SerializeOptions& opts, int depth);

void render_node(std::string& out, const Node& node, const SerializeOptions& opts,
                 int depth) {
  append_indent(out, depth, opts.indent_width);
  switch (node.kind) {
    case NodeKind::kAssignment:
      render_path(out, node.path);
      out += " = ";
      render_value(out, node.value, opts, depth);
      out += ";\n";
      break;
    case NodeKind::kBlock:
      out += node.name;
      if (node.has_label) {
        out.push_back(' ');
        append_quoted(out, node.label);
      }
      if (node.children.empty()) {
        out += " {}\n";
      } else {
        out += " {\n";
        render_nodes(out, node.children, opts, depth + 1);
        append_indent(out, depth, opts.indent_width);
        out += "}\n";
      }
      break;
    case NodeKind::kInclude:
      out += "include ";
      append_quoted(out, node.include_path);
      out += ";\n";
      break;
  }
}

void render_nodes(std::string& out, const std::vector<Node>& nodes,
                  const SerializeOptions& opts, int depth) {
  for (const Node& node : nodes) {
    render_node(out, node, opts, depth);
  }
}

}  // namespace

std::string escape_string(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
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
        if (c < 0x20 || c >= 0x7F) {
          append_hex_byte(out, c);
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::string serialize_value(const Value& value, const SerializeOptions& opts) {
  std::string out;
  render_value(out, value, opts, 0);
  return out;
}

std::string serialize(const Document& doc, const SerializeOptions& opts) {
  std::string out;
  render_nodes(out, doc.nodes, opts, 0);
  return out;
}

}  // namespace cfgscript
}  // namespace bw
