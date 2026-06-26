// bytewright - cfgscript expression evaluator
//
// Folds an Expr tree into a Value against a scoped Environment. The semantics
// aim to be the obvious C-like ones:
//   * arithmetic promotes int->float when either operand is float;
//   * '/' and '%' by zero throw kStateViolation;
//   * bitwise and shift operators require integer operands (kBadType);
//   * '+' concatenates when either side is a string;
//   * comparisons work numerically and on strings;
//   * '&&' / '||' short-circuit and yield a Bool;
//   * an unknown identifier reference throws kStateViolation.
//
// The evaluator shares kMaxDepth with the parser to bound recursion on deeply
// nested expressions.
#ifndef BYTEWRIGHT_CFGSCRIPT_EVALUATOR_HPP
#define BYTEWRIGHT_CFGSCRIPT_EVALUATOR_HPP

#include "cfgscript/ast.hpp"
#include "cfgscript/environment.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

// Evaluate `expr` in `env`. `depth` is the current recursion depth and is
// checked against kMaxDepth.
Value evaluate(const Expr& expr, const Environment& env, int depth);

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_EVALUATOR_HPP
