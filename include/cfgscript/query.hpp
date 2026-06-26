// bytewright - cfgscript path query
//
// Looks up values inside an evaluated Document by a dotted path string such as
// "server.ports[0]" or "limits.max". The query understands three kinds of step:
//
//   * a block name or an assignment leaf inside the current scope (dot step),
//   * an object member access (dot step into a Value of object type),
//   * an array index `[n]` appended to the previous step.
//
// Resolution starts at the document's top level. Each dotted component first
// matches a child block of that name (descending into the block), otherwise an
// assignment whose path tail matches. Once a Value is reached, further `.member`
// steps index object literals and `[n]` steps index arrays.
//
// Lookups never throw: a missing path returns an empty optional, and the typed
// accessors return their fallback when the type does not match.
#ifndef BYTEWRIGHT_CFGSCRIPT_QUERY_HPP
#define BYTEWRIGHT_CFGSCRIPT_QUERY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

// Resolve `path` against the document, returning the matched Value if present.
std::optional<Value> query(const Document& doc, const std::string& path);

// A bound query result with typed, non-throwing accessors. Construct via
// find(); test validity with `has_value()`.
class QueryResult {
 public:
  QueryResult() = default;
  explicit QueryResult(Value value) : value_(std::move(value)), found_(true) {}

  bool has_value() const noexcept { return found_; }
  explicit operator bool() const noexcept { return found_; }

  // The underlying Value. Only meaningful when has_value() is true.
  const Value& value() const noexcept { return value_; }

  // Typed accessors. Each returns `fallback` if the result is absent or the
  // stored value is not of the requested type. Integers are also produced from
  // floats (truncating) and vice versa so numeric paths are convenient.
  std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
  double as_double(double fallback = 0.0) const noexcept;
  bool as_bool(bool fallback = false) const noexcept;
  std::string as_string(const std::string& fallback = std::string()) const;

  // Returns the array contents, or an empty array if the value is not an array.
  const ValueArray& as_array() const noexcept;

  // Returns the object contents, or an empty object if not an object.
  const ValueObject& as_object() const noexcept;

 private:
  Value value_;
  bool found_ = false;
};

// Resolve `path` and wrap the outcome in a QueryResult for typed access.
QueryResult find(const Document& doc, const std::string& path);

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_QUERY_HPP
