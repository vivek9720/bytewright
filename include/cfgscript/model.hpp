// bytewright - cfgscript document model
//
// The result of parsing+evaluating a config is a Document: an ordered list of
// statement nodes. Each node is one of:
//   * AssignmentNode  -- `a.b.c = <expr>` with the expression already evaluated
//                        into a Value.
//   * BlockNode       -- `name { ... }` or `name "label" { ... }`, holding its
//                        own ordered child nodes.
//   * IncludeNode     -- `include "path";`. The path is stored as DATA ONLY.
//                        cfgscript never opens or reads the referenced file.
//
// We use an explicit discriminated Node (enum + struct) rather than std::variant
// to keep summarize()'s recursive walk and the model's construction obvious.
#ifndef BYTEWRIGHT_CFGSCRIPT_MODEL_HPP
#define BYTEWRIGHT_CFGSCRIPT_MODEL_HPP

#include <string>
#include <vector>

#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

enum class NodeKind {
  kAssignment,
  kBlock,
  kInclude,
};

struct Node {
  NodeKind kind = NodeKind::kAssignment;

  // kAssignment: dotted path components (e.g. {"server","port"}) and value.
  std::vector<std::string> path;
  Value value;

  // kBlock: block name, optional string label, and child statements.
  std::string name;
  bool has_label = false;
  std::string label;
  std::vector<Node> children;

  // kInclude: the literal include path (never resolved against the filesystem).
  std::string include_path;
};

// The evaluated configuration tree handed back from parse().
struct Document {
  std::vector<Node> nodes;
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_MODEL_HPP
