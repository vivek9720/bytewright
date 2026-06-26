// bytewright - cfgscript expression AST
//
// The parser builds an expression tree of Expr nodes; the evaluator folds them
// into a Value. Statements (assignments, blocks, includes) are not represented
// as AST nodes here -- the document driver consumes them directly and stores
// evaluated results in the model (see model.hpp). Only expressions need a tree,
// because expressions are the part that is evaluated recursively.
//
// Nodes are held through std::unique_ptr so the tree owns its children and is
// destroyed bottom-up without manual cleanup.
#ifndef BYTEWRIGHT_CFGSCRIPT_AST_HPP
#define BYTEWRIGHT_CFGSCRIPT_AST_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfgscript/token.hpp"

namespace bw {
namespace cfgscript {

enum class ExprKind {
  kNull,
  kBool,
  kInt,
  kFloat,
  kStr,
  kIdent,    // bare identifier reference resolved against the environment
  kArray,    // [ a, b, ... ]
  kObject,   // { key = expr; ... }
  kUnary,    // - ! ~  applied to one operand
  kBinary,   // any binary operator (TokenKind in `op`)
  kTernary,  // cond ? a : b
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  ExprKind kind;
  std::size_t offset = 0;  // byte offset for error reporting

  // Leaf payloads.
  bool bool_value = false;
  std::int64_t int_value = 0;
  double float_value = 0.0;
  std::string str_value;  // kStr literal text or kIdent name

  // Operators.
  TokenKind op = TokenKind::kEof;  // kUnary / kBinary operator token
  ExprPtr lhs;                     // unary operand / binary left / ternary cond
  ExprPtr rhs;                     // binary right / ternary "then"
  ExprPtr third;                   // ternary "else"

  // Aggregate children.
  std::vector<ExprPtr> elements;                       // kArray
  std::vector<std::pair<std::string, ExprPtr>> fields;  // kObject

  explicit Expr(ExprKind k, std::size_t off) : kind(k), offset(off) {}
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_AST_HPP
