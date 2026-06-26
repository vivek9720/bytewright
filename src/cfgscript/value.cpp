// bytewright - cfgscript Value helpers.
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

const char* value_type_name(ValueType type) noexcept {
  switch (type) {
    case ValueType::kNull: return "null";
    case ValueType::kBool: return "bool";
    case ValueType::kInt: return "int";
    case ValueType::kFloat: return "float";
    case ValueType::kStr: return "string";
    case ValueType::kArray: return "array";
    case ValueType::kObject: return "object";
  }
  return "value";
}

bool Value::truthy() const noexcept {
  switch (type) {
    case ValueType::kNull: return false;
    case ValueType::kBool: return b;
    case ValueType::kInt: return i != 0;
    case ValueType::kFloat: return f != 0.0;
    case ValueType::kStr: return !s.empty();
    case ValueType::kArray: return !array.empty();
    case ValueType::kObject: return !object.empty();
  }
  return false;
}

}  // namespace cfgscript
}  // namespace bw
