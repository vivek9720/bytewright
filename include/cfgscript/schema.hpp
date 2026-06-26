// bytewright - cfgscript schema validation
//
// A small, in-memory schema describes what an evaluated Document should contain:
// for each dotted path, an expected ValueType, whether it is required, optional
// numeric min/max bounds, and an optional set of allowed string values. The
// validator resolves each rule against the document (reusing the path query) and
// returns a list of structured violations. It never throws -- malformed or
// missing data produces violations, not exceptions.
//
// The schema is intentionally minimal (flat list of per-path rules) rather than
// a full recursive type system; it covers the common config-checking needs of
// "this key must exist, be an int, and lie in [1, 65535]".
#ifndef BYTEWRIGHT_CFGSCRIPT_SCHEMA_HPP
#define BYTEWRIGHT_CFGSCRIPT_SCHEMA_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

// A single rule constraining the value at one dotted path.
struct FieldRule {
  std::string path;                 // e.g. "server.port"
  ValueType type = ValueType::kStr; // expected type
  bool required = true;             // a missing required path is a violation

  // Numeric bounds (inclusive). Applied only when type is kInt or kFloat and the
  // corresponding has_* flag is set.
  bool has_min = false;
  double min_value = 0.0;
  bool has_max = false;
  double max_value = 0.0;

  // Allowed string values. When non-empty and type is kStr, the value must be
  // one of these.
  std::vector<std::string> allowed;
};

// A schema is an ordered collection of field rules plus a convenient builder.
class Schema {
 public:
  Schema& require(std::string path, ValueType type) {
    FieldRule rule;
    rule.path = std::move(path);
    rule.type = type;
    rule.required = true;
    rules_.push_back(std::move(rule));
    return *this;
  }

  Schema& optional(std::string path, ValueType type) {
    FieldRule rule;
    rule.path = std::move(path);
    rule.type = type;
    rule.required = false;
    rules_.push_back(std::move(rule));
    return *this;
  }

  // Attach numeric bounds to the most recently added rule.
  Schema& range(double min_value, double max_value) {
    if (!rules_.empty()) {
      rules_.back().has_min = true;
      rules_.back().min_value = min_value;
      rules_.back().has_max = true;
      rules_.back().max_value = max_value;
    }
    return *this;
  }

  Schema& min(double min_value) {
    if (!rules_.empty()) {
      rules_.back().has_min = true;
      rules_.back().min_value = min_value;
    }
    return *this;
  }

  Schema& max(double max_value) {
    if (!rules_.empty()) {
      rules_.back().has_max = true;
      rules_.back().max_value = max_value;
    }
    return *this;
  }

  // Restrict the most recently added rule to a set of allowed strings.
  Schema& enum_values(std::vector<std::string> values) {
    if (!rules_.empty()) rules_.back().allowed = std::move(values);
    return *this;
  }

  // Add a fully-built rule directly.
  Schema& add(FieldRule rule) {
    rules_.push_back(std::move(rule));
    return *this;
  }

  const std::vector<FieldRule>& rules() const noexcept { return rules_; }

 private:
  std::vector<FieldRule> rules_;
};

// Why a value failed its rule. Stable enough to branch on in tests / reports.
enum class ViolationKind {
  kMissingRequired,  // required path absent from the document
  kTypeMismatch,     // present but wrong ValueType
  kBelowMinimum,     // numeric value below the rule's minimum
  kAboveMaximum,     // numeric value above the rule's maximum
  kNotAllowed,       // string value not in the allowed set
};

// One structured validation failure: which path, why, and a human message.
struct Violation {
  ViolationKind kind;
  std::string path;
  std::string message;
};

// Validate `doc` against `schema`, returning every violation found (empty means
// the document satisfies the schema). Never throws.
std::vector<Violation> validate(const Document& doc, const Schema& schema);

// Human-readable name for a violation kind (for reports / test messages).
const char* violation_kind_name(ViolationKind kind) noexcept;

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_SCHEMA_HPP
