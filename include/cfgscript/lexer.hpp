// bytewright - cfgscript lexer
//
// A hand-written tokenizer over a non-owning byte buffer. It treats the input
// as text but tolerates arbitrary bytes (bytes >= 0x80 are only legal inside
// string literals, where they pass through verbatim). On any malformed token it
// throws bw::common::ParseError with the offending byte offset.
//
// The lexer tracks line/column purely to enrich error messages; the offset it
// passes to ParseError is always the absolute byte position, per repo policy.
#ifndef BYTEWRIGHT_CFGSCRIPT_LEXER_HPP
#define BYTEWRIGHT_CFGSCRIPT_LEXER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfgscript/token.hpp"

namespace bw {
namespace cfgscript {

class Lexer {
 public:
  Lexer(const std::uint8_t* data, std::size_t size);

  // Produce the next token. Returns a kEof token (repeatedly) once the input is
  // exhausted. Skips whitespace and comments. Throws on malformed input.
  Token next();

  std::size_t offset() const noexcept { return pos_; }

 private:
  // Character classification helpers (ASCII only; bytes are unsigned).
  static bool is_ident_start(std::uint8_t c) noexcept;
  static bool is_ident_part(std::uint8_t c) noexcept;
  static bool is_digit(std::uint8_t c) noexcept;
  static bool is_hex_digit(std::uint8_t c) noexcept;
  static int hex_value(std::uint8_t c) noexcept;

  std::uint8_t peek(std::size_t ahead = 0) const noexcept;
  bool at_end() const noexcept { return pos_ >= size_; }
  std::uint8_t advance() noexcept;  // consume one byte, update line/column

  void skip_trivia();  // whitespace + all three comment styles

  Token lex_ident_or_keyword();
  Token lex_number();
  Token lex_string();

  // Append the UTF-8 encoding of a Unicode scalar value to `out`.
  void append_utf8(std::string& out, std::uint32_t code_point,
                   std::size_t err_offset) const;

  [[noreturn]] void error(std::size_t at, const std::string& msg) const;

  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_LEXER_HPP
