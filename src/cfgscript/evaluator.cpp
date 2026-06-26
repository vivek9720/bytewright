// bytewright - cfgscript expression evaluator implementation.
//
// Walks an Expr tree and folds it into a Value. The arithmetic follows the
// "promote to double when either operand is float" rule; integer operations stay
// in int64. Errors are reported as ParseError carrying the offending expression's
// byte offset.
#include "cfgscript/evaluator.hpp"

#include <cstdint>
#include <string>

#include "cfgscript/parser.hpp"  // for kMaxDepth
#include "common/status.hpp"

namespace bw {
namespace cfgscript {

using common::ErrorCode;
using common::ParseError;

namespace {

[[noreturn]] void eval_error(ErrorCode code, std::size_t at,
                             const std::string& msg) {
  throw ParseError(code, at, msg);
}

void require_depth(int depth, std::size_t at) {
  if (depth > kMaxDepth) {
    eval_error(ErrorCode::kDepthExceeded, at, "expression nested too deep");
  }
}

// Three-way numeric comparison returning <0, 0, >0. Mixed int/float compares as
// doubles; two ints compare exactly.
int compare_numbers(const Value& a, const Value& b) {
  if (a.type == ValueType::kInt && b.type == ValueType::kInt) {
    if (a.i < b.i) return -1;
    if (a.i > b.i) return 1;
    return 0;
  }
  double x = a.as_double();
  double y = b.as_double();
  if (x < y) return -1;
  if (x > y) return 1;
  return 0;
}

// Equality across the data model. Numbers compare numerically (int vs float
// allowed); otherwise both sides must share a type.
bool values_equal(const Value& a, const Value& b) {
  if (a.is_number() && b.is_number()) {
    return compare_numbers(a, b) == 0;
  }
  if (a.type != b.type) return false;
  switch (a.type) {
    case ValueType::kNull: return true;
    case ValueType::kBool: return a.b == b.b;
    case ValueType::kStr: return a.s == b.s;
    case ValueType::kArray: {
      if (a.array.size() != b.array.size()) return false;
      for (std::size_t k = 0; k < a.array.size(); ++k) {
        if (!values_equal(a.array[k], b.array[k])) return false;
      }
      return true;
    }
    case ValueType::kObject: {
      if (a.object.size() != b.object.size()) return false;
      for (std::size_t k = 0; k < a.object.size(); ++k) {
        if (a.object[k].first != b.object[k].first) return false;
        if (!values_equal(a.object[k].second, b.object[k].second)) return false;
      }
      return true;
    }
    default: return false;  // numbers handled above
  }
}

std::string to_concat_string(const Value& v) {
  switch (v.type) {
    case ValueType::kStr: return v.s;
    case ValueType::kInt: return std::to_string(v.i);
    case ValueType::kFloat: return std::to_string(v.f);
    case ValueType::kBool: return v.b ? "true" : "false";
    case ValueType::kNull: return "null";
    default: return std::string();
  }
}

std::int64_t require_int(const Value& v, std::size_t at, const char* op) {
  if (v.type != ValueType::kInt) {
    eval_error(ErrorCode::kBadType, at,
               std::string("operator '") + op +
                   "' requires integer operands");
  }
  return v.i;
}

// Arithmetic for + - * with int/float promotion. '+' on strings concatenates.
Value eval_arith(TokenKind op, const Value& l, const Value& r,
                 std::size_t at) {
  if (op == TokenKind::kPlus &&
      (l.type == ValueType::kStr || r.type == ValueType::kStr)) {
    return Value::str(to_concat_string(l) + to_concat_string(r));
  }
  if (!l.is_number() || !r.is_number()) {
    eval_error(ErrorCode::kBadType, at,
               "arithmetic requires numeric operands");
  }
  bool floaty = l.type == ValueType::kFloat || r.type == ValueType::kFloat;
  if (floaty) {
    double x = l.as_double();
    double y = r.as_double();
    switch (op) {
      case TokenKind::kPlus: return Value::floating(x + y);
      case TokenKind::kMinus: return Value::floating(x - y);
      case TokenKind::kStar: return Value::floating(x * y);
      default: break;
    }
  } else {
    std::int64_t x = l.i;
    std::int64_t y = r.i;
    switch (op) {
      // Cast through uint64 for +,-,* so signed overflow is well-defined
      // wraparound rather than UB (the values are config constants, but a
      // fuzzer can still feed overflowing arithmetic).
      case TokenKind::kPlus:
        return Value::integer(static_cast<std::int64_t>(
            static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y)));
      case TokenKind::kMinus:
        return Value::integer(static_cast<std::int64_t>(
            static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y)));
      case TokenKind::kStar:
        return Value::integer(static_cast<std::int64_t>(
            static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y)));
      default: break;
    }
  }
  eval_error(ErrorCode::kBadType, at, "unsupported arithmetic operator");
}

Value eval_div_mod(TokenKind op, const Value& l, const Value& r,
                   std::size_t at) {
  if (!l.is_number() || !r.is_number()) {
    eval_error(ErrorCode::kBadType, at, "division requires numeric operands");
  }
  bool floaty = l.type == ValueType::kFloat || r.type == ValueType::kFloat;
  if (floaty) {
    double y = r.as_double();
    if (y == 0.0) {
      eval_error(ErrorCode::kStateViolation, at, "division by zero");
    }
    double x = l.as_double();
    if (op == TokenKind::kSlash) return Value::floating(x / y);
    // '%' on floats: reject rather than invent fmod semantics.
    eval_error(ErrorCode::kBadType, at, "'%' requires integer operands");
  }
  std::int64_t y = r.i;
  if (y == 0) {
    eval_error(ErrorCode::kStateViolation, at,
               op == TokenKind::kSlash ? "division by zero" : "modulo by zero");
  }
  std::int64_t x = l.i;
  // Guard the INT64_MIN / -1 overflow case explicitly.
  if (y == -1 && x == INT64_MIN) {
    return op == TokenKind::kSlash ? Value::integer(INT64_MIN)
                                   : Value::integer(0);
  }
  if (op == TokenKind::kSlash) return Value::integer(x / y);
  return Value::integer(x % y);
}

}  // namespace

Value evaluate(const Expr& expr, const Environment& env, int depth) {
  require_depth(depth, expr.offset);

  switch (expr.kind) {
    case ExprKind::kNull: return Value::null();
    case ExprKind::kBool: return Value::boolean(expr.bool_value);
    case ExprKind::kInt: return Value::integer(expr.int_value);
    case ExprKind::kFloat: return Value::floating(expr.float_value);
    case ExprKind::kStr: return Value::str(expr.str_value);

    case ExprKind::kIdent: {
      const Value* found = env.lookup(expr.str_value);
      if (found == nullptr) {
        eval_error(ErrorCode::kStateViolation, expr.offset,
                   "unknown identifier '" + expr.str_value + "'");
      }
      return *found;
    }

    case ExprKind::kArray: {
      ValueArray items;
      items.reserve(expr.elements.size());
      for (const ExprPtr& el : expr.elements) {
        items.push_back(evaluate(*el, env, depth + 1));
      }
      return Value::make_array(std::move(items));
    }

    case ExprKind::kObject: {
      ValueObject fields;
      fields.reserve(expr.fields.size());
      for (const auto& kv : expr.fields) {
        fields.emplace_back(kv.first, evaluate(*kv.second, env, depth + 1));
      }
      return Value::make_object(std::move(fields));
    }

    case ExprKind::kUnary: {
      Value v = evaluate(*expr.lhs, env, depth + 1);
      switch (expr.op) {
        case TokenKind::kMinus:
          if (v.type == ValueType::kInt) {
            return Value::integer(static_cast<std::int64_t>(
                0u - static_cast<std::uint64_t>(v.i)));
          }
          if (v.type == ValueType::kFloat) return Value::floating(-v.f);
          eval_error(ErrorCode::kBadType, expr.offset,
                     "unary '-' requires a number");
        case TokenKind::kBang:
          return Value::boolean(!v.truthy());
        case TokenKind::kTilde: {
          std::int64_t iv = require_int(v, expr.offset, "~");
          return Value::integer(~iv);
        }
        default:
          eval_error(ErrorCode::kBadType, expr.offset,
                     "unsupported unary operator");
      }
    }

    case ExprKind::kTernary: {
      Value cond = evaluate(*expr.lhs, env, depth + 1);
      if (cond.truthy()) return evaluate(*expr.rhs, env, depth + 1);
      return evaluate(*expr.third, env, depth + 1);
    }

    case ExprKind::kBinary: {
      // Logical operators short-circuit, so evaluate the right side lazily.
      if (expr.op == TokenKind::kAndAnd) {
        Value l = evaluate(*expr.lhs, env, depth + 1);
        if (!l.truthy()) return Value::boolean(false);
        Value r = evaluate(*expr.rhs, env, depth + 1);
        return Value::boolean(r.truthy());
      }
      if (expr.op == TokenKind::kOrOr) {
        Value l = evaluate(*expr.lhs, env, depth + 1);
        if (l.truthy()) return Value::boolean(true);
        Value r = evaluate(*expr.rhs, env, depth + 1);
        return Value::boolean(r.truthy());
      }

      Value l = evaluate(*expr.lhs, env, depth + 1);
      Value r = evaluate(*expr.rhs, env, depth + 1);
      switch (expr.op) {
        case TokenKind::kPlus:
        case TokenKind::kMinus:
        case TokenKind::kStar:
          return eval_arith(expr.op, l, r, expr.offset);
        case TokenKind::kSlash:
        case TokenKind::kPercent:
          return eval_div_mod(expr.op, l, r, expr.offset);

        case TokenKind::kShl:
        case TokenKind::kShr: {
          std::int64_t a = require_int(l, expr.offset, "shift");
          std::int64_t b = require_int(r, expr.offset, "shift");
          if (b < 0 || b > 63) {
            eval_error(ErrorCode::kStateViolation, expr.offset,
                       "shift amount out of range");
          }
          std::uint64_t ua = static_cast<std::uint64_t>(a);
          std::uint64_t sh = static_cast<std::uint64_t>(b);
          if (expr.op == TokenKind::kShl) {
            return Value::integer(static_cast<std::int64_t>(ua << sh));
          }
          // Arithmetic right shift on the signed value (sign-preserving).
          return Value::integer(a >> sh);
        }

        case TokenKind::kAmp:
          return Value::integer(require_int(l, expr.offset, "&") &
                                require_int(r, expr.offset, "&"));
        case TokenKind::kCaret:
          return Value::integer(require_int(l, expr.offset, "^") ^
                                require_int(r, expr.offset, "^"));
        case TokenKind::kPipe:
          return Value::integer(require_int(l, expr.offset, "|") |
                                require_int(r, expr.offset, "|"));

        case TokenKind::kEq:
          return Value::boolean(values_equal(l, r));
        case TokenKind::kNe:
          return Value::boolean(!values_equal(l, r));

        case TokenKind::kLt:
        case TokenKind::kLe:
        case TokenKind::kGt:
        case TokenKind::kGe: {
          int cmp = 0;
          if (l.is_number() && r.is_number()) {
            cmp = compare_numbers(l, r);
          } else if (l.type == ValueType::kStr && r.type == ValueType::kStr) {
            cmp = l.s.compare(r.s);
            cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
          } else {
            eval_error(ErrorCode::kBadType, expr.offset,
                       "comparison requires two numbers or two strings");
          }
          switch (expr.op) {
            case TokenKind::kLt: return Value::boolean(cmp < 0);
            case TokenKind::kLe: return Value::boolean(cmp <= 0);
            case TokenKind::kGt: return Value::boolean(cmp > 0);
            case TokenKind::kGe: return Value::boolean(cmp >= 0);
            default: break;
          }
          eval_error(ErrorCode::kBadType, expr.offset, "bad comparison");
        }

        default:
          eval_error(ErrorCode::kBadType, expr.offset,
                     "unsupported binary operator");
      }
    }
  }

  eval_error(ErrorCode::kBadType, expr.offset, "unevaluable expression");
}

}  // namespace cfgscript
}  // namespace bw
