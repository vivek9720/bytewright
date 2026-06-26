// bytewright - cfgscript flatten / diff implementation.
//
// Flattening walks the same model the serializer does, but instead of emitting
// source it accumulates one (path, scalar-text) pair per leaf. Blocks contribute
// their name (and label, when present, as a `[label]`-style discriminator) to
// the prefix; assignments contribute their dotted path; arrays and objects
// recurse with `[n]` / `.member` suffixes.
//
// The diff is an ordered key/value comparison: build an index of a's keys, then
// scan b to find added/changed keys, and scan a to find removed keys.
#include "cfgscript/flatten.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cfgscript/model.hpp"
#include "cfgscript/serializer.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

namespace {

std::string join_path(const std::string& prefix, const std::string& leaf) {
  if (prefix.empty()) return leaf;
  std::string out = prefix;
  out.push_back('.');
  out += leaf;
  return out;
}

std::string join_index(const std::string& prefix, std::size_t index) {
  std::string out = prefix;
  out.push_back('[');
  out += std::to_string(index);
  out.push_back(']');
  return out;
}

void flatten_into(const std::string& prefix, const Value& value,
                  std::vector<FlatEntry>& out) {
  switch (value.type) {
    case ValueType::kNull:
    case ValueType::kBool:
    case ValueType::kInt:
    case ValueType::kFloat:
    case ValueType::kStr:
      out.push_back(FlatEntry{prefix, render_scalar(value)});
      break;
    case ValueType::kArray:
      if (value.array.empty()) {
        // Record empty aggregates so add/remove of an empty list is visible.
        out.push_back(FlatEntry{prefix, "[]"});
      } else {
        for (std::size_t k = 0; k < value.array.size(); ++k) {
          flatten_into(join_index(prefix, k), value.array[k], out);
        }
      }
      break;
    case ValueType::kObject:
      if (value.object.empty()) {
        out.push_back(FlatEntry{prefix, "{}"});
      } else {
        for (const auto& kv : value.object) {
          flatten_into(join_path(prefix, kv.first), kv.second, out);
        }
      }
      break;
  }
}

// Combine a block's name and optional label into a single prefix component.
std::string block_prefix(const std::string& prefix, const Node& block) {
  std::string name = block.name;
  if (block.has_label) {
    name.push_back('[');
    name += block.label;
    name.push_back(']');
  }
  return join_path(prefix, name);
}

void flatten_nodes(const std::string& prefix, const std::vector<Node>& nodes,
                   std::vector<FlatEntry>& out) {
  for (const Node& node : nodes) {
    switch (node.kind) {
      case NodeKind::kAssignment: {
        std::string path = prefix;
        for (const std::string& component : node.path) {
          path = join_path(path, component);
        }
        flatten_into(path, node.value, out);
        break;
      }
      case NodeKind::kBlock:
        flatten_nodes(block_prefix(prefix, node), node.children, out);
        break;
      case NodeKind::kInclude:
        out.push_back(
            FlatEntry{join_path(prefix, "include"), node.include_path});
        break;
    }
  }
}

}  // namespace

std::string render_scalar(const Value& value) {
  switch (value.type) {
    case ValueType::kNull:
      return "null";
    case ValueType::kBool:
      return value.b ? "true" : "false";
    case ValueType::kInt:
      return std::to_string(value.i);
    case ValueType::kFloat:
      return std::to_string(value.f);
    case ValueType::kStr:
      // Quote+escape so a string "1" never collides with the integer 1.
      return std::string("\"") + escape_string(value.s) + "\"";
    case ValueType::kArray:
      return "[]";
    case ValueType::kObject:
      return "{}";
  }
  return "null";
}

std::vector<FlatEntry> flatten_value(const std::string& prefix,
                                     const Value& value) {
  std::vector<FlatEntry> out;
  flatten_into(prefix, value, out);
  return out;
}

std::vector<FlatEntry> flatten(const Document& doc) {
  std::vector<FlatEntry> out;
  flatten_nodes(std::string(), doc.nodes, out);
  return out;
}

std::vector<DiffEntry> diff(const std::vector<FlatEntry>& a,
                            const std::vector<FlatEntry>& b) {
  std::vector<DiffEntry> out;

  // Index a's entries by path for O(1) lookup. If a path repeats (legal in the
  // model), the last occurrence wins, mirroring last-write semantics.
  std::unordered_map<std::string, std::string> a_index;
  a_index.reserve(a.size());
  for (const FlatEntry& e : a) a_index[e.path] = e.value;

  std::unordered_map<std::string, std::string> b_index;
  b_index.reserve(b.size());
  for (const FlatEntry& e : b) b_index[e.path] = e.value;

  // Removed keys, in a's order (skip duplicates already emitted).
  std::unordered_map<std::string, bool> seen;
  for (const FlatEntry& e : a) {
    if (seen.count(e.path)) continue;
    seen[e.path] = true;
    if (b_index.find(e.path) == b_index.end()) {
      DiffEntry d;
      d.kind = DiffKind::kRemoved;
      d.path = e.path;
      d.old_value = a_index[e.path];
      out.push_back(std::move(d));
    }
  }

  // Added and changed keys, in b's order.
  seen.clear();
  for (const FlatEntry& e : b) {
    if (seen.count(e.path)) continue;
    seen[e.path] = true;
    auto it = a_index.find(e.path);
    if (it == a_index.end()) {
      DiffEntry d;
      d.kind = DiffKind::kAdded;
      d.path = e.path;
      d.new_value = b_index[e.path];
      out.push_back(std::move(d));
    } else if (it->second != b_index[e.path]) {
      DiffEntry d;
      d.kind = DiffKind::kChanged;
      d.path = e.path;
      d.old_value = it->second;
      d.new_value = b_index[e.path];
      out.push_back(std::move(d));
    }
  }
  return out;
}

std::vector<DiffEntry> diff(const Document& a, const Document& b) {
  return diff(flatten(a), flatten(b));
}

}  // namespace cfgscript
}  // namespace bw
