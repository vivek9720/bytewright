// bytewright - cfgscript flatten / diff
//
// Flattens an evaluated Document into an ordered list of (dotted-path, rendered
// scalar) pairs, recursing through blocks, assignment paths, object literals and
// arrays. Array elements use a `[n]` suffix; object members and block names use
// `.member`. The rendered scalar is a stable textual form of each leaf Value
// (null/bool/int/float/string), which makes two flattened configs trivially
// comparable.
//
// diff(a, b) pairs the two flattened views by key and reports which keys were
// added, removed, or changed between them -- the building block for a config
// "what changed" report.
#ifndef BYTEWRIGHT_CFGSCRIPT_FLATTEN_HPP
#define BYTEWRIGHT_CFGSCRIPT_FLATTEN_HPP

#include <string>
#include <vector>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

// One leaf of a flattened document: a fully-qualified dotted path and the
// stable textual rendering of its scalar value.
struct FlatEntry {
  std::string path;
  std::string value;
};

// Flatten the whole document. Order follows the document's node order (and,
// within values, array/object element order), so the result is deterministic.
std::vector<FlatEntry> flatten(const Document& doc);

// Flatten a single Value under a given path prefix. Exposed so callers can
// flatten sub-trees (e.g. a queried value) on their own.
std::vector<FlatEntry> flatten_value(const std::string& prefix,
                                     const Value& value);

// Render one scalar (or aggregate marker) Value to the stable text used by
// flatten(). Aggregates are not rendered here (flatten recurses into them); this
// is only called for leaf scalars.
std::string render_scalar(const Value& value);

// Classification of a single key when diffing two flattened documents.
enum class DiffKind {
  kAdded,    // present in b, absent in a
  kRemoved,  // present in a, absent in b
  kChanged,  // present in both with different rendered values
};

// One entry in a diff result.
struct DiffEntry {
  DiffKind kind;
  std::string path;
  std::string old_value;  // empty for kAdded
  std::string new_value;  // empty for kRemoved
};

// Compute the ordered differences turning document `a` into document `b`.
// Removed keys come first (in a's order), then added/changed keys in b's order,
// giving a stable, readable change list.
std::vector<DiffEntry> diff(const Document& a, const Document& b);

// Overload diffing pre-flattened views, for callers that already have them.
std::vector<DiffEntry> diff(const std::vector<FlatEntry>& a,
                            const std::vector<FlatEntry>& b);

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_FLATTEN_HPP
