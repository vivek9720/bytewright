// bytewright - cfgscript path query implementation.
//
// A path is tokenized into a flat list of steps, each either a name (identifier
// matched against block names, assignment path components and object members) or
// an array index. Resolution proceeds in two phases:
//
//   1. Structural phase: while we are still positioned at a list of model Nodes
//      (the document top level or a block's children), name steps descend into
//      child blocks or match assignment paths. This phase ends as soon as we
//      reach a concrete Value (an assignment's value, or a block exhausted).
//   2. Value phase: remaining steps index into the reached Value -- name steps
//      select object members, index steps select array elements.
//
// Nothing here throws; an unmatched step yields an empty optional.
#include "cfgscript/query.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

namespace {

struct Step {
  bool is_index = false;
  std::string name;     // valid when !is_index
  std::size_t index = 0;  // valid when is_index
};

// Tokenize "a.b[2].c" into [name a][name b][index 2][name c]. Returns false on
// malformed syntax (empty name, unterminated bracket, non-digit index) so the
// query reports "not found" rather than guessing.
bool tokenize(const std::string& path, std::vector<Step>& out) {
  std::size_t i = 0;
  const std::size_t n = path.size();
  // Leading '.' is not allowed; an empty path is treated as no steps.
  while (i < n) {
    if (path[i] == '.') {
      // A '.' must separate two names and cannot start the path or be doubled.
      if (out.empty()) return false;
      ++i;
      if (i >= n) return false;  // trailing dot
    }
    if (path[i] == '[') {
      // Index step. Must follow an existing step.
      if (out.empty()) return false;
      ++i;
      std::size_t value = 0;
      bool any = false;
      while (i < n && path[i] >= '0' && path[i] <= '9') {
        value = value * 10 + static_cast<std::size_t>(path[i] - '0');
        any = true;
        ++i;
      }
      if (!any) return false;
      if (i >= n || path[i] != ']') return false;
      ++i;  // consume ']'
      Step step;
      step.is_index = true;
      step.index = value;
      out.push_back(step);
      continue;
    }
    // Name step: read until the next '.', '[' or end.
    std::size_t start = i;
    while (i < n && path[i] != '.' && path[i] != '[') ++i;
    if (i == start) return false;  // empty name
    Step step;
    step.is_index = false;
    step.name = path.substr(start, i - start);
    out.push_back(step);
  }
  return true;
}

// Navigate within a Value using the steps from `pos` onward. Returns the matched
// Value, or empty if a step does not apply to the current value shape.
std::optional<Value> walk_value(const Value& start, const std::vector<Step>& steps,
                                std::size_t pos) {
  const Value* current = &start;
  for (; pos < steps.size(); ++pos) {
    const Step& step = steps[pos];
    if (step.is_index) {
      if (current->type != ValueType::kArray) return std::nullopt;
      if (step.index >= current->array.size()) return std::nullopt;
      current = &current->array[step.index];
    } else {
      if (current->type != ValueType::kObject) return std::nullopt;
      const Value* next = nullptr;
      for (const auto& kv : current->object) {
        if (kv.first == step.name) {
          next = &kv.second;
          break;
        }
      }
      if (next == nullptr) return std::nullopt;
      current = next;
    }
  }
  return *current;
}

// Try to match an assignment node's dotted path against the steps starting at
// `pos`. On success, returns the number of steps consumed; on failure returns 0.
std::size_t match_assignment_path(const Node& node,
                                  const std::vector<Step>& steps,
                                  std::size_t pos) {
  std::size_t consumed = 0;
  for (const std::string& component : node.path) {
    const std::size_t at = pos + consumed;
    if (at >= steps.size()) return 0;  // path longer than remaining steps
    const Step& step = steps[at];
    if (step.is_index || step.name != component) return 0;
    ++consumed;
  }
  return consumed;
}

// Structural resolution over a list of nodes. Consumes name steps to descend
// blocks / match assignments, then hands off to walk_value once a Value is hit.
std::optional<Value> walk_nodes(const std::vector<Node>& nodes,
                                const std::vector<Step>& steps, std::size_t pos);

std::optional<Value> resolve_in_block(const Node& block,
                                      const std::vector<Step>& steps,
                                      std::size_t pos) {
  return walk_nodes(block.children, steps, pos);
}

std::optional<Value> walk_nodes(const std::vector<Node>& nodes,
                                const std::vector<Step>& steps,
                                std::size_t pos) {
  if (pos >= steps.size()) return std::nullopt;
  const Step& step = steps[pos];
  if (step.is_index) return std::nullopt;  // cannot index a node list

  // Prefer a matching block: it lets the caller continue descending.
  for (const Node& node : nodes) {
    if (node.kind == NodeKind::kBlock && node.name == step.name) {
      auto found = resolve_in_block(node, steps, pos + 1);
      if (found.has_value()) return found;
    }
  }

  // Otherwise look for an assignment whose dotted path matches a prefix of the
  // remaining steps; the rest (if any) navigate into its Value.
  for (const Node& node : nodes) {
    if (node.kind != NodeKind::kAssignment || node.path.empty()) continue;
    std::size_t consumed = match_assignment_path(node, steps, pos);
    if (consumed != node.path.size()) continue;
    return walk_value(node.value, steps, pos + consumed);
  }
  return std::nullopt;
}

}  // namespace

std::optional<Value> query(const Document& doc, const std::string& path) {
  std::vector<Step> steps;
  if (!tokenize(path, steps)) return std::nullopt;
  if (steps.empty()) return std::nullopt;
  return walk_nodes(doc.nodes, steps, 0);
}

QueryResult find(const Document& doc, const std::string& path) {
  std::optional<Value> v = query(doc, path);
  if (!v.has_value()) return QueryResult{};
  return QueryResult{std::move(*v)};
}

std::int64_t QueryResult::as_int(std::int64_t fallback) const noexcept {
  if (!found_) return fallback;
  if (value_.type == ValueType::kInt) return value_.i;
  if (value_.type == ValueType::kFloat) {
    return static_cast<std::int64_t>(value_.f);
  }
  return fallback;
}

double QueryResult::as_double(double fallback) const noexcept {
  if (!found_) return fallback;
  if (value_.type == ValueType::kFloat) return value_.f;
  if (value_.type == ValueType::kInt) {
    return static_cast<double>(value_.i);
  }
  return fallback;
}

bool QueryResult::as_bool(bool fallback) const noexcept {
  if (!found_) return fallback;
  if (value_.type == ValueType::kBool) return value_.b;
  return fallback;
}

std::string QueryResult::as_string(const std::string& fallback) const {
  if (!found_) return fallback;
  if (value_.type == ValueType::kStr) return value_.s;
  return fallback;
}

const ValueArray& QueryResult::as_array() const noexcept {
  static const ValueArray kEmpty;
  if (!found_ || value_.type != ValueType::kArray) return kEmpty;
  return value_.array;
}

const ValueObject& QueryResult::as_object() const noexcept {
  static const ValueObject kEmpty;
  if (!found_ || value_.type != ValueType::kObject) return kEmpty;
  return value_.object;
}

}  // namespace cfgscript
}  // namespace bw
