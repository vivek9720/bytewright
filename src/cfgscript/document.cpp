// bytewright - cfgscript document driver and pretty-printer.
//
// This translation unit ties the pieces together: it parses the source into a
// statement list, then walks that list evaluating each expression against a
// scoped Environment to build the model Document. Each block creates a child
// scope; assignments bind their leaf name into the current scope so that later
// expressions (in the same or nested scopes) can reference them.
//
// It also implements summarize(), the human-readable dump used by bwdump.
#include "cfgscript/cfgscript.hpp"

#include <string>
#include <vector>

#include "cfgscript/environment.hpp"
#include "cfgscript/evaluator.hpp"
#include "cfgscript/parser.hpp"
#include "cfgscript/value.hpp"
#include "common/hexdump.hpp"
#include "common/status.hpp"

namespace bw {
namespace cfgscript {

namespace {

// Recursively turn a parsed statement list into model nodes, evaluating every
// expression against `env`. Blocks recurse with a fresh child environment.
std::vector<Node> build_nodes(const std::vector<ParsedStatement>& stmts,
                              Environment& env) {
  std::vector<Node> out;
  out.reserve(stmts.size());

  for (const ParsedStatement& st : stmts) {
    switch (st.kind) {
      case StmtKind::kAssignment: {
        Node node;
        node.kind = NodeKind::kAssignment;
        node.path = st.path;
        node.value = evaluate(*st.expr, env, 1);
        // Bind the assignment so later expressions can reference it. We bind by
        // the leaf component of the dotted path (e.g. `a.b.c = 1` binds "c"),
        // which is enough for the reference resolution the language promises.
        if (!st.path.empty()) {
          env.define(st.path.back(), node.value);
        }
        out.push_back(std::move(node));
        break;
      }
      case StmtKind::kBlock: {
        Node node;
        node.kind = NodeKind::kBlock;
        node.name = st.name;
        node.has_label = st.has_label;
        node.label = st.label;
        // Each block introduces a child scope chained to the enclosing one.
        Environment child(&env);
        node.children = build_nodes(st.children, child);
        out.push_back(std::move(node));
        break;
      }
      case StmtKind::kInclude: {
        Node node;
        node.kind = NodeKind::kInclude;
        node.include_path = st.include_path;  // DATA ONLY: never opened.
        out.push_back(std::move(node));
        break;
      }
    }
  }
  return out;
}

// --- pretty printing -----------------------------------------------------

void append_indent(std::string& out, int depth) {
  for (int k = 0; k < depth; ++k) out += "  ";
}

void render_value(std::string& out, const Value& v);

void render_array(std::string& out, const ValueArray& arr) {
  out.push_back('[');
  for (std::size_t k = 0; k < arr.size(); ++k) {
    if (k != 0) out += ", ";
    render_value(out, arr[k]);
  }
  out.push_back(']');
}

void render_object(std::string& out, const ValueObject& obj) {
  out.push_back('{');
  for (std::size_t k = 0; k < obj.size(); ++k) {
    if (k != 0) out += ", ";
    out += obj[k].first;
    out += " = ";
    render_value(out, obj[k].second);
  }
  out.push_back('}');
}

void render_value(std::string& out, const Value& v) {
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
      out += std::to_string(v.f);
      break;
    case ValueType::kStr:
      out.push_back('"');
      // Reuse the shared escaper so control bytes and non-ASCII render safely.
      out += common::escape(v.s);
      out.push_back('"');
      break;
    case ValueType::kArray:
      render_array(out, v.array);
      break;
    case ValueType::kObject:
      render_object(out, v.object);
      break;
  }
}

void render_path(std::string& out, const std::vector<std::string>& path) {
  for (std::size_t k = 0; k < path.size(); ++k) {
    if (k != 0) out.push_back('.');
    out += path[k];
  }
}

void render_nodes(std::string& out, const std::vector<Node>& nodes, int depth) {
  for (const Node& node : nodes) {
    append_indent(out, depth);
    switch (node.kind) {
      case NodeKind::kAssignment:
        render_path(out, node.path);
        out += " = ";
        render_value(out, node.value);
        out.push_back('\n');
        break;
      case NodeKind::kBlock:
        out += node.name;
        if (node.has_label) {
          out += " \"";
          out += common::escape(node.label);
          out.push_back('"');
        }
        out += " {\n";
        render_nodes(out, node.children, depth + 1);
        append_indent(out, depth);
        out += "}\n";
        break;
      case NodeKind::kInclude:
        out += "include \"";
        out += common::escape(node.include_path);
        out += "\"\n";
        break;
    }
  }
}

}  // namespace

Document parse(const std::uint8_t* data, std::size_t size) {
  Parser parser(data, size);
  std::vector<ParsedStatement> stmts = parser.parse_document();

  Document doc;
  Environment root;
  doc.nodes = build_nodes(stmts, root);
  return doc;
}

Document parse(const std::string& text) {
  return parse(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

std::string summarize(const Document& doc) {
  std::string out;
  out += "cfgscript document: ";
  out += std::to_string(doc.nodes.size());
  out += doc.nodes.size() == 1 ? " top-level node\n" : " top-level nodes\n";
  render_nodes(out, doc.nodes, 0);
  return out;
}

}  // namespace cfgscript
}  // namespace bw
