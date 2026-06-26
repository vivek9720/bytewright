// bytewright - cfgscript token model
//
// The lexer turns an input buffer into a flat stream of Tokens. Each token
// carries its kind, the absolute byte offset where it began (used both for
// ParseError reporting and for human-friendly line/column rendering), and any
// decoded payload. String literals are decoded eagerly: `text` holds the bytes
// after escape processing, so the parser never has to re-scan the source.
#ifndef BYTEWRIGHT_CFGSCRIPT_TOKEN_HPP
#define BYTEWRIGHT_CFGSCRIPT_TOKEN_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace bw {
namespace cfgscript {

// Token kinds. The single-character punctuators and the multi-character
// operators each get a distinct kind so the parser can switch without peeking
// at the lexeme text again.
enum class TokenKind {
  kEof,

  // Literals and names.
  kIdent,   // identifier or unrecognized keyword
  kInt,     // decimal or 0x hex integer  -> int_value
  kFloat,   // floating point literal     -> float_value
  kString,  // decoded string literal     -> text

  // Keywords.
  kTrue,
  kFalse,
  kNull,
  kInclude,

  // Punctuation.
  kLBrace,    // {
  kRBrace,    // }
  kLBracket,  // [
  kRBracket,  // ]
  kLParen,    // (
  kRParen,    // )
  kSemicolon, // ;
  kComma,     // ,
  kAssign,    // =
  kDot,       // .
  kQuestion,  // ?
  kColon,     // :

  // Operators.
  kPlus,     // +
  kMinus,    // -
  kStar,     // *
  kSlash,    // /
  kPercent,  // %
  kShl,      // <<
  kShr,      // >>
  kLt,       // <
  kLe,       // <=
  kGt,       // >
  kGe,       // >=
  kEq,       // ==
  kNe,       // !=
  kAmp,      // &
  kCaret,    // ^
  kPipe,     // |
  kAndAnd,   // &&
  kOrOr,     // ||
  kBang,     // !
  kTilde,    // ~
};

const char* token_kind_name(TokenKind kind) noexcept;

struct Token {
  TokenKind kind = TokenKind::kEof;
  std::size_t offset = 0;  // absolute byte offset of the first character

  // Payloads. Only the field matching `kind` is meaningful.
  std::string text;          // kIdent, kString (decoded), keywords (lexeme)
  std::int64_t int_value = 0;  // kInt
  double float_value = 0.0;    // kFloat
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_TOKEN_HPP
