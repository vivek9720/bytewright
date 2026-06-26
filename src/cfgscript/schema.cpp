// bytewright - cfgscript schema validation implementation.
//
// For each rule we resolve the path with query(). An absent path is a
// MissingRequired violation when the rule is required, and silently fine when
// optional. A present value is checked for type, then (for numbers) range, then
// (for strings) membership in the allowed set. Every check appends a structured
// Violation rather than throwing, so a caller gets the full list in one pass.
#include "cfgscript/schema.hpp"

#include <optional>
#include <string>
#include <vector>

#include "cfgscript/model.hpp"
#include "cfgscript/query.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

namespace {

bool type_matches(const Value& value, ValueType expected) {
  if (value.type == expected) return true;
  // A rule asking for kFloat also accepts an integer literal, since cfgscript
  // distinguishes the two but a "number" field is commonly written either way.
  if (expected == ValueType::kFloat && value.type == ValueType::kInt) {
    return true;
  }
  return false;
}

// Numeric magnitude for range checks; only called when the value is numeric.
double numeric_value(const Value& value) { return value.as_double(); }

void check_range(const FieldRule& rule, const Value& value,
                 std::vector<Violation>& out) {
  if (!value.is_number()) return;
  const double n = numeric_value(value);
  if (rule.has_min && n < rule.min_value) {
    Violation v;
    v.kind = ViolationKind::kBelowMinimum;
    v.path = rule.path;
    v.message = "value " + std::to_string(n) + " is below minimum " +
                std::to_string(rule.min_value);
    out.push_back(std::move(v));
  }
  if (rule.has_max && n > rule.max_value) {
    Violation v;
    v.kind = ViolationKind::kAboveMaximum;
    v.path = rule.path;
    v.message = "value " + std::to_string(n) + " is above maximum " +
                std::to_string(rule.max_value);
    out.push_back(std::move(v));
  }
}

void check_allowed(const FieldRule& rule, const Value& value,
                   std::vector<Violation>& out) {
  if (rule.allowed.empty() || value.type != ValueType::kStr) return;
  for (const std::string& candidate : rule.allowed) {
    if (candidate == value.s) return;
  }
  Violation v;
  v.kind = ViolationKind::kNotAllowed;
  v.path = rule.path;
  v.message = "value \"" + value.s + "\" is not an allowed option";
  out.push_back(std::move(v));
}

}  // namespace

const char* violation_kind_name(ViolationKind kind) noexcept {
  switch (kind) {
    case ViolationKind::kMissingRequired: return "missing-required";
    case ViolationKind::kTypeMismatch: return "type-mismatch";
    case ViolationKind::kBelowMinimum: return "below-minimum";
    case ViolationKind::kAboveMaximum: return "above-maximum";
    case ViolationKind::kNotAllowed: return "not-allowed";
  }
  return "violation";
}

std::vector<Violation> validate(const Document& doc, const Schema& schema) {
  std::vector<Violation> out;
  for (const FieldRule& rule : schema.rules()) {
    std::optional<Value> resolved = query(doc, rule.path);
    if (!resolved.has_value()) {
      if (rule.required) {
        Violation v;
        v.kind = ViolationKind::kMissingRequired;
        v.path = rule.path;
        v.message = "required path is absent";
        out.push_back(std::move(v));
      }
      continue;
    }

    const Value& value = *resolved;
    if (!type_matches(value, rule.type)) {
      Violation v;
      v.kind = ViolationKind::kTypeMismatch;
      v.path = rule.path;
      v.message = std::string("expected ") + value_type_name(rule.type) +
                  " but found " + value_type_name(value.type);
      out.push_back(std::move(v));
      // A type mismatch makes range/enum checks meaningless; skip them.
      continue;
    }

    check_range(rule, value, out);
    check_allowed(rule, value, out);
  }
  return out;
}

}  // namespace cfgscript
}  // namespace bw
