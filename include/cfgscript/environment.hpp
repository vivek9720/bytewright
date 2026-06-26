// bytewright - cfgscript scoped symbol environment
//
// Identifier references in expressions resolve against an Environment: a chain
// of scopes. Each block introduces a child scope whose `parent` points at the
// enclosing one. Lookup walks outward (current scope first, then parents) so an
// inner block can shadow or reference outer names. Top-level assignments live in
// the root scope.
//
// The environment only stores already-evaluated Values, so it is a plain
// ordered map of name -> Value with no behavioural surprises.
#ifndef BYTEWRIGHT_CFGSCRIPT_ENVIRONMENT_HPP
#define BYTEWRIGHT_CFGSCRIPT_ENVIRONMENT_HPP

#include <string>
#include <unordered_map>

#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

class Environment {
 public:
  Environment() = default;
  explicit Environment(const Environment* parent) : parent_(parent) {}

  // Bind (or rebind) a name in this scope only.
  void define(const std::string& name, Value value) {
    table_[name] = std::move(value);
  }

  // Resolve a name walking outward through enclosing scopes. Returns nullptr if
  // the name is unknown in every reachable scope.
  const Value* lookup(const std::string& name) const {
    for (const Environment* env = this; env != nullptr; env = env->parent_) {
      auto it = env->table_.find(name);
      if (it != env->table_.end()) {
        return &it->second;
      }
    }
    return nullptr;
  }

 private:
  const Environment* parent_ = nullptr;
  std::unordered_map<std::string, Value> table_;
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_ENVIRONMENT_HPP
