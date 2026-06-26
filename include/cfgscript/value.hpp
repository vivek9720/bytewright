// bytewright - cfgscript runtime value
//
// Expressions evaluate to a Value: a small dynamically-typed variant covering
// the JSON-ish data model plus a 64-bit integer type distinct from float. We
// use an explicit discriminated struct rather than std::variant so the
// recursive Array/Object members are easy to spell and the evaluator's type
// switches stay readable.
//
// Object preserves insertion order (it is a vector of key/value pairs) because a
// config file's key order is meaningful when we pretty-print it back out.
#ifndef BYTEWRIGHT_CFGSCRIPT_VALUE_HPP
#define BYTEWRIGHT_CFGSCRIPT_VALUE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bw {
namespace cfgscript {

enum class ValueType {
  kNull,
  kBool,
  kInt,
  kFloat,
  kStr,
  kArray,
  kObject,
};

const char* value_type_name(ValueType type) noexcept;

struct Value;
using ValueArray = std::vector<Value>;
using ValueObject = std::vector<std::pair<std::string, Value>>;

struct Value {
  ValueType type = ValueType::kNull;

  bool b = false;
  std::int64_t i = 0;
  double f = 0.0;
  std::string s;
  // Array/Object children. Held by value; Value is movable/copyable so nested
  // structures compose naturally. (No pointers are required because the data
  // is a finite tree built bottom-up by the evaluator.)
  ValueArray array;
  ValueObject object;

  Value() = default;

  static Value null() { return Value{}; }
  static Value boolean(bool v) {
    Value out;
    out.type = ValueType::kBool;
    out.b = v;
    return out;
  }
  static Value integer(std::int64_t v) {
    Value out;
    out.type = ValueType::kInt;
    out.i = v;
    return out;
  }
  static Value floating(double v) {
    Value out;
    out.type = ValueType::kFloat;
    out.f = v;
    return out;
  }
  static Value str(std::string v) {
    Value out;
    out.type = ValueType::kStr;
    out.s = std::move(v);
    return out;
  }
  static Value make_array(ValueArray v) {
    Value out;
    out.type = ValueType::kArray;
    out.array = std::move(v);
    return out;
  }
  static Value make_object(ValueObject v) {
    Value out;
    out.type = ValueType::kObject;
    out.object = std::move(v);
    return out;
  }

  bool is_number() const noexcept {
    return type == ValueType::kInt || type == ValueType::kFloat;
  }

  // Numeric coercion used by float-promoting arithmetic and comparisons.
  double as_double() const noexcept {
    return type == ValueType::kFloat ? f : static_cast<double>(i);
  }

  // Truthiness for logical operators and ternary conditions: null and false are
  // falsey, 0 / 0.0 / "" are falsey, everything else is truthy.
  bool truthy() const noexcept;
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_VALUE_HPP
