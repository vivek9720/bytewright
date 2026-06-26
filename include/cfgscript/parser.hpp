// bytewright - cfgscript recursive-descent parser
//
// Grammar (EBNF-ish; `*` = zero-or-more, `?` = optional):
//
//   document    := statement* EOF
//   statement   := ';'                                  (* empty *)
//                | 'include' STRING ';'                 (* include record *)
//                | IDENT STRING? '{' statement* '}'     (* named block *)
//                | dotted '=' expr ';'                  (* assignment *)
//   dotted      := IDENT ('.' IDENT)*
//
//   (* expressions, lowest precedence first; see parser.cpp for the climbing
//      table. ternary is right-associative, the rest are left-associative. *)
//   expr        := ternary
//   ternary     := logic_or ('?' expr ':' expr)?
//   logic_or    := logic_and ('||' logic_and)*
//   logic_and   := bit_or    ('&&' bit_or)*
//   bit_or      := bit_xor   ('|'  bit_xor)*
//   bit_xor     := bit_and   ('^'  bit_and)*
//   bit_and     := equality  ('&'  equality)*
//   equality    := relational (('==' | '!=') relational)*
//   relational  := shift      (('<' | '<=' | '>' | '>=') shift)*
//   shift       := additive   (('<<' | '>>') additive)*
//   additive    := multiply   (('+' | '-') multiply)*
//   multiply    := unary      (('*' | '/' | '%') unary)*
//   unary       := ('-' | '!' | '~') unary | primary
//   primary     := NUMBER | STRING | 'true' | 'false' | 'null'
//                | IDENT | array | object | '(' expr ')'
//   array       := '[' (expr (',' expr)* ','?)? ']'
//   object      := '{' (IDENT '=' expr ';')* '}'
//
// A note on the {-ambiguity: at statement level a leading IDENT followed by '{'
// (or by STRING '{') is a block; otherwise it begins a dotted assignment. At
// expression level a '{' always starts an object literal.
//
// Parsing and evaluation are interleaved by the Document driver so that
// identifier references can be resolved against the scope built up so far.
#ifndef BYTEWRIGHT_CFGSCRIPT_PARSER_HPP
#define BYTEWRIGHT_CFGSCRIPT_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cfgscript/ast.hpp"
#include "cfgscript/lexer.hpp"
#include "cfgscript/token.hpp"

namespace bw {
namespace cfgscript {

// Recursion guard limit shared by the parser and evaluator. Pathological input
// is reachable immediately (there is no header), so this cap is what keeps the
// recursive descent from overflowing the stack -> kDepthExceeded.
constexpr int kMaxDepth = 64;

// A statement produced by the parser, before/independent of evaluation. The
// document driver turns these into model Nodes, evaluating expressions as it
// goes. Blocks recurse into nested ParsedStatement lists.
enum class StmtKind {
  kAssignment,
  kBlock,
  kInclude,
};

struct ParsedStatement {
  StmtKind kind = StmtKind::kAssignment;
  std::size_t offset = 0;

  // kAssignment.
  std::vector<std::string> path;
  ExprPtr expr;

  // kBlock.
  std::string name;
  bool has_label = false;
  std::string label;
  std::vector<ParsedStatement> children;

  // kInclude.
  std::string include_path;
};

class Parser {
 public:
  Parser(const std::uint8_t* data, std::size_t size);

  // Parse the whole input into a statement list. Throws on syntax errors.
  std::vector<ParsedStatement> parse_document();

 private:
  // Token stream management. We keep one token of lookahead in `cur_`.
  const Token& peek() const { return cur_; }
  Token take();                       // return cur_ and advance
  bool check(TokenKind kind) const { return cur_.kind == kind; }
  bool accept(TokenKind kind);        // advance and return true on match
  Token expect(TokenKind kind, const char* what);  // or throw

  // Statement productions.
  ParsedStatement parse_statement(int depth);
  ParsedStatement parse_block(const std::string& name, std::size_t offset,
                              int depth);
  ParsedStatement parse_assignment(std::vector<std::string> path,
                                   std::size_t offset);

  // Expression productions (precedence climbing in parse_binary).
  ExprPtr parse_expr(int depth);
  ExprPtr parse_ternary(int depth);
  ExprPtr parse_binary(int min_prec, int depth);
  ExprPtr parse_unary(int depth);
  ExprPtr parse_primary(int depth);
  ExprPtr parse_array(std::size_t offset, int depth);
  ExprPtr parse_object(std::size_t offset, int depth);

  void check_depth(int depth, std::size_t at) const;
  [[noreturn]] void error(std::size_t at, const std::string& msg) const;

  Lexer lexer_;
  Token cur_;
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_PARSER_HPP
