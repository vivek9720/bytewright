// bytewright - cfgscript recursive-descent parser implementation.
//
// See parser.hpp for the grammar. Expressions use precedence climbing: a single
// parse_binary(min_prec) loop drives every left-associative binary level from a
// precedence table, with unary and primary below it and the ternary on top.
#include "cfgscript/parser.hpp"

#include <string>
#include <utility>

#include "common/status.hpp"

namespace bw {
namespace cfgscript {

using common::ErrorCode;
using common::ParseError;

namespace {

// Binary operator precedence. Higher binds tighter. Returns -1 for tokens that
// are not binary operators. Mirrors the usual C ordering.
int binary_precedence(TokenKind kind) {
  switch (kind) {
    case TokenKind::kStar:
    case TokenKind::kSlash:
    case TokenKind::kPercent: return 11;
    case TokenKind::kPlus:
    case TokenKind::kMinus: return 10;
    case TokenKind::kShl:
    case TokenKind::kShr: return 9;
    case TokenKind::kLt:
    case TokenKind::kLe:
    case TokenKind::kGt:
    case TokenKind::kGe: return 8;
    case TokenKind::kEq:
    case TokenKind::kNe: return 7;
    case TokenKind::kAmp: return 6;
    case TokenKind::kCaret: return 5;
    case TokenKind::kPipe: return 4;
    case TokenKind::kAndAnd: return 3;
    case TokenKind::kOrOr: return 2;
    default: return -1;
  }
}

}  // namespace

Parser::Parser(const std::uint8_t* data, std::size_t size)
    : lexer_(data, size) {
  cur_ = lexer_.next();
}

Token Parser::take() {
  Token t = std::move(cur_);
  cur_ = lexer_.next();
  return t;
}

bool Parser::accept(TokenKind kind) {
  if (cur_.kind == kind) {
    take();
    return true;
  }
  return false;
}

Token Parser::expect(TokenKind kind, const char* what) {
  if (cur_.kind != kind) {
    error(cur_.offset, std::string("expected ") + what + ", found " +
                           token_kind_name(cur_.kind));
  }
  return take();
}

void Parser::error(std::size_t at, const std::string& msg) const {
  throw ParseError(ErrorCode::kBadType, at, msg);
}

void Parser::check_depth(int depth, std::size_t at) const {
  if (depth > kMaxDepth) {
    throw ParseError(ErrorCode::kDepthExceeded, at, "nesting too deep");
  }
}

std::vector<ParsedStatement> Parser::parse_document() {
  std::vector<ParsedStatement> stmts;
  while (!check(TokenKind::kEof)) {
    // A bare ';' is an empty statement; skip it without producing a node.
    if (accept(TokenKind::kSemicolon)) continue;
    stmts.push_back(parse_statement(1));
  }
  return stmts;
}

ParsedStatement Parser::parse_statement(int depth) {
  check_depth(depth, peek().offset);

  std::size_t offset = peek().offset;

  // include "string" ;
  if (check(TokenKind::kInclude)) {
    take();
    Token path = expect(TokenKind::kString, "string path after 'include'");
    expect(TokenKind::kSemicolon, "';' after include path");
    ParsedStatement st;
    st.kind = StmtKind::kInclude;
    st.offset = offset;
    st.include_path = std::move(path.text);
    return st;
  }

  // Everything else begins with an identifier: either a block (IDENT '{' or
  // IDENT STRING '{') or a dotted assignment (IDENT ('.' IDENT)* '=' ...).
  Token first = expect(TokenKind::kIdent, "identifier at start of statement");
  std::string name = std::move(first.text);

  // Block with a label: `name "label" { ... }`.
  if (check(TokenKind::kString)) {
    Token label = take();
    if (!check(TokenKind::kLBrace)) {
      error(peek().offset,
            "expected '{' after block label");
    }
    ParsedStatement st = parse_block(name, offset, depth);
    st.has_label = true;
    st.label = std::move(label.text);
    return st;
  }

  // Block without a label: `name { ... }`.
  if (check(TokenKind::kLBrace)) {
    return parse_block(name, offset, depth);
  }

  // Otherwise a dotted-path assignment.
  std::vector<std::string> path;
  path.push_back(std::move(name));
  while (accept(TokenKind::kDot)) {
    Token part = expect(TokenKind::kIdent, "identifier after '.'");
    path.push_back(std::move(part.text));
  }
  return parse_assignment(std::move(path), offset);
}

ParsedStatement Parser::parse_block(const std::string& name, std::size_t offset,
                                    int depth) {
  expect(TokenKind::kLBrace, "'{' to open block");
  ParsedStatement st;
  st.kind = StmtKind::kBlock;
  st.offset = offset;
  st.name = name;
  while (!check(TokenKind::kRBrace)) {
    if (check(TokenKind::kEof)) {
      error(offset, "unterminated block (missing '}')");
    }
    if (accept(TokenKind::kSemicolon)) continue;
    st.children.push_back(parse_statement(depth + 1));
  }
  expect(TokenKind::kRBrace, "'}' to close block");
  return st;
}

ParsedStatement Parser::parse_assignment(std::vector<std::string> path,
                                         std::size_t offset) {
  expect(TokenKind::kAssign, "'=' in assignment");
  ExprPtr expr = parse_expr(1);
  expect(TokenKind::kSemicolon, "';' after assignment");
  ParsedStatement st;
  st.kind = StmtKind::kAssignment;
  st.offset = offset;
  st.path = std::move(path);
  st.expr = std::move(expr);
  return st;
}

// --- expressions ---------------------------------------------------------

ExprPtr Parser::parse_expr(int depth) { return parse_ternary(depth); }

ExprPtr Parser::parse_ternary(int depth) {
  check_depth(depth, peek().offset);
  ExprPtr cond = parse_binary(1, depth);
  if (check(TokenKind::kQuestion)) {
    std::size_t off = take().offset;
    ExprPtr then_branch = parse_expr(depth + 1);
    expect(TokenKind::kColon, "':' in ternary expression");
    ExprPtr else_branch = parse_expr(depth + 1);  // right-associative
    auto node = std::make_unique<Expr>(ExprKind::kTernary, off);
    node->lhs = std::move(cond);
    node->rhs = std::move(then_branch);
    node->third = std::move(else_branch);
    return node;
  }
  return cond;
}

ExprPtr Parser::parse_binary(int min_prec, int depth) {
  check_depth(depth, peek().offset);
  ExprPtr lhs = parse_unary(depth + 1);
  for (;;) {
    int prec = binary_precedence(peek().kind);
    if (prec < min_prec) break;
    Token op = take();
    // All binary operators here are left-associative, so the right operand is
    // parsed with a strictly higher minimum precedence.
    ExprPtr rhs = parse_binary(prec + 1, depth + 1);
    auto node = std::make_unique<Expr>(ExprKind::kBinary, op.offset);
    node->op = op.kind;
    node->lhs = std::move(lhs);
    node->rhs = std::move(rhs);
    lhs = std::move(node);
  }
  return lhs;
}

ExprPtr Parser::parse_unary(int depth) {
  check_depth(depth, peek().offset);
  if (check(TokenKind::kMinus) || check(TokenKind::kBang) ||
      check(TokenKind::kTilde)) {
    Token op = take();
    ExprPtr operand = parse_unary(depth + 1);
    auto node = std::make_unique<Expr>(ExprKind::kUnary, op.offset);
    node->op = op.kind;
    node->lhs = std::move(operand);
    return node;
  }
  return parse_primary(depth);
}

ExprPtr Parser::parse_primary(int depth) {
  check_depth(depth, peek().offset);
  const Token& t = peek();
  std::size_t off = t.offset;

  switch (t.kind) {
    case TokenKind::kInt: {
      auto node = std::make_unique<Expr>(ExprKind::kInt, off);
      node->int_value = t.int_value;
      take();
      return node;
    }
    case TokenKind::kFloat: {
      auto node = std::make_unique<Expr>(ExprKind::kFloat, off);
      node->float_value = t.float_value;
      take();
      return node;
    }
    case TokenKind::kString: {
      auto node = std::make_unique<Expr>(ExprKind::kStr, off);
      node->str_value = t.text;
      take();
      return node;
    }
    case TokenKind::kTrue: {
      auto node = std::make_unique<Expr>(ExprKind::kBool, off);
      node->bool_value = true;
      take();
      return node;
    }
    case TokenKind::kFalse: {
      auto node = std::make_unique<Expr>(ExprKind::kBool, off);
      node->bool_value = false;
      take();
      return node;
    }
    case TokenKind::kNull: {
      take();
      return std::make_unique<Expr>(ExprKind::kNull, off);
    }
    case TokenKind::kIdent: {
      auto node = std::make_unique<Expr>(ExprKind::kIdent, off);
      node->str_value = t.text;
      take();
      return node;
    }
    case TokenKind::kLParen: {
      take();
      ExprPtr inner = parse_expr(depth + 1);
      expect(TokenKind::kRParen, "')' to close parenthesized expression");
      return inner;
    }
    case TokenKind::kLBracket:
      return parse_array(off, depth);
    case TokenKind::kLBrace:
      return parse_object(off, depth);
    default:
      error(off, std::string("expected an expression, found ") +
                     token_kind_name(t.kind));
  }
}

ExprPtr Parser::parse_array(std::size_t offset, int depth) {
  check_depth(depth, offset);
  expect(TokenKind::kLBracket, "'['");
  auto node = std::make_unique<Expr>(ExprKind::kArray, offset);
  if (!check(TokenKind::kRBracket)) {
    for (;;) {
      node->elements.push_back(parse_expr(depth + 1));
      if (accept(TokenKind::kComma)) {
        // Allow a trailing comma before the closing bracket.
        if (check(TokenKind::kRBracket)) break;
        continue;
      }
      break;
    }
  }
  expect(TokenKind::kRBracket, "']' to close array literal");
  return node;
}

ExprPtr Parser::parse_object(std::size_t offset, int depth) {
  check_depth(depth, offset);
  expect(TokenKind::kLBrace, "'{'");
  auto node = std::make_unique<Expr>(ExprKind::kObject, offset);
  while (!check(TokenKind::kRBrace)) {
    if (check(TokenKind::kEof)) {
      error(offset, "unterminated object literal (missing '}')");
    }
    // Empty members are allowed (a stray ';').
    if (accept(TokenKind::kSemicolon)) continue;
    Token key = expect(TokenKind::kIdent, "key identifier in object literal");
    expect(TokenKind::kAssign, "'=' after object key");
    ExprPtr value = parse_expr(depth + 1);
    expect(TokenKind::kSemicolon, "';' after object field");
    node->fields.emplace_back(std::move(key.text), std::move(value));
  }
  expect(TokenKind::kRBrace, "'}' to close object literal");
  return node;
}

}  // namespace cfgscript
}  // namespace bw
