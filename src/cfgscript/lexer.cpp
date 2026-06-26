// bytewright - cfgscript lexer implementation.
//
// Hand-written scanner. Whitespace and comments are skipped between tokens;
// every other byte must begin a valid token or the lexer throws a ParseError at
// the offending offset. String literals are decoded eagerly into their byte
// payload (escapes resolved, \u{...} UTF-8 encoded).
#include "cfgscript/lexer.hpp"

#include <string>

#include "common/status.hpp"

namespace bw {
namespace cfgscript {

using common::ErrorCode;
using common::ParseError;

Lexer::Lexer(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {}

bool Lexer::is_ident_start(std::uint8_t c) noexcept {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool Lexer::is_ident_part(std::uint8_t c) noexcept {
  return is_ident_start(c) || is_digit(c);
}

bool Lexer::is_digit(std::uint8_t c) noexcept { return c >= '0' && c <= '9'; }

bool Lexer::is_hex_digit(std::uint8_t c) noexcept {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int Lexer::hex_value(std::uint8_t c) noexcept {
  if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a') + 10;
  if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A') + 10;
  return -1;
}

std::uint8_t Lexer::peek(std::size_t ahead) const noexcept {
  std::size_t at = pos_ + ahead;
  return at < size_ ? data_[at] : static_cast<std::uint8_t>(0);
}

std::uint8_t Lexer::advance() noexcept {
  std::uint8_t c = data_[pos_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

void Lexer::error(std::size_t at, const std::string& msg) const {
  std::string full = msg + " (line " + std::to_string(line_) + ", column " +
                     std::to_string(column_) + ")";
  throw ParseError(ErrorCode::kBadType, at, full);
}

void Lexer::skip_trivia() {
  for (;;) {
    if (at_end()) return;
    std::uint8_t c = peek();
    // Whitespace.
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v') {
      advance();
      continue;
    }
    // Line comments: '#' to EOL and '//' to EOL.
    if (c == '#') {
      while (!at_end() && peek() != '\n') advance();
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      advance();
      advance();
      while (!at_end() && peek() != '\n') advance();
      continue;
    }
    // Block comments: /* ... */. Non-nested (C-style): the first */ closes it.
    if (c == '/' && peek(1) == '*') {
      std::size_t start = pos_;
      advance();  // '/'
      advance();  // '*'
      bool closed = false;
      while (!at_end()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) error(start, "unterminated block comment");
      continue;
    }
    return;
  }
}

Token Lexer::next() {
  skip_trivia();

  Token tok;
  tok.offset = pos_;
  if (at_end()) {
    tok.kind = TokenKind::kEof;
    return tok;
  }

  std::uint8_t c = peek();

  if (is_ident_start(c)) return lex_ident_or_keyword();
  if (is_digit(c)) return lex_number();
  if (c == '"') return lex_string();

  // A leading '.' followed by a digit is still a stray dot here: floats in this
  // language require a leading digit (".5" is not accepted), so '.' is punctuation.

  std::size_t start = pos_;
  advance();  // consume the first character of the punctuator/operator

  switch (c) {
    case '{': tok.kind = TokenKind::kLBrace; return tok;
    case '}': tok.kind = TokenKind::kRBrace; return tok;
    case '[': tok.kind = TokenKind::kLBracket; return tok;
    case ']': tok.kind = TokenKind::kRBracket; return tok;
    case '(': tok.kind = TokenKind::kLParen; return tok;
    case ')': tok.kind = TokenKind::kRParen; return tok;
    case ';': tok.kind = TokenKind::kSemicolon; return tok;
    case ',': tok.kind = TokenKind::kComma; return tok;
    case '.': tok.kind = TokenKind::kDot; return tok;
    case '?': tok.kind = TokenKind::kQuestion; return tok;
    case ':': tok.kind = TokenKind::kColon; return tok;
    case '+': tok.kind = TokenKind::kPlus; return tok;
    case '-': tok.kind = TokenKind::kMinus; return tok;
    case '*': tok.kind = TokenKind::kStar; return tok;
    case '/': tok.kind = TokenKind::kSlash; return tok;
    case '%': tok.kind = TokenKind::kPercent; return tok;
    case '^': tok.kind = TokenKind::kCaret; return tok;
    case '~': tok.kind = TokenKind::kTilde; return tok;
    case '=':
      if (peek() == '=') { advance(); tok.kind = TokenKind::kEq; }
      else tok.kind = TokenKind::kAssign;
      return tok;
    case '!':
      if (peek() == '=') { advance(); tok.kind = TokenKind::kNe; }
      else tok.kind = TokenKind::kBang;
      return tok;
    case '<':
      if (peek() == '<') { advance(); tok.kind = TokenKind::kShl; }
      else if (peek() == '=') { advance(); tok.kind = TokenKind::kLe; }
      else tok.kind = TokenKind::kLt;
      return tok;
    case '>':
      if (peek() == '>') { advance(); tok.kind = TokenKind::kShr; }
      else if (peek() == '=') { advance(); tok.kind = TokenKind::kGe; }
      else tok.kind = TokenKind::kGt;
      return tok;
    case '&':
      if (peek() == '&') { advance(); tok.kind = TokenKind::kAndAnd; }
      else tok.kind = TokenKind::kAmp;
      return tok;
    case '|':
      if (peek() == '|') { advance(); tok.kind = TokenKind::kOrOr; }
      else tok.kind = TokenKind::kPipe;
      return tok;
    default:
      break;
  }

  error(start, "unexpected character");
}

Token Lexer::lex_ident_or_keyword() {
  Token tok;
  tok.offset = pos_;
  std::size_t start = pos_;
  while (!at_end() && is_ident_part(peek())) advance();
  std::string word(reinterpret_cast<const char*>(data_ + start), pos_ - start);

  if (word == "true") tok.kind = TokenKind::kTrue;
  else if (word == "false") tok.kind = TokenKind::kFalse;
  else if (word == "null") tok.kind = TokenKind::kNull;
  else if (word == "include") tok.kind = TokenKind::kInclude;
  else tok.kind = TokenKind::kIdent;

  tok.text = std::move(word);
  return tok;
}

Token Lexer::lex_number() {
  Token tok;
  tok.offset = pos_;
  std::size_t start = pos_;

  // Hexadecimal integer: 0x / 0X followed by one or more hex digits.
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    advance();  // '0'
    advance();  // 'x'
    std::size_t digits_start = pos_;
    std::uint64_t acc = 0;
    while (!at_end() && is_hex_digit(peek())) {
      int v = hex_value(peek());
      acc = acc * 16u + static_cast<std::uint64_t>(v);
      advance();
    }
    if (pos_ == digits_start) error(start, "hex literal has no digits");
    tok.kind = TokenKind::kInt;
    tok.int_value = static_cast<std::int64_t>(acc);
    return tok;
  }

  // Decimal: integer part, optional fractional part, optional exponent. We let
  // the standard library do the final numeric conversion once we know the span
  // and whether it is integer- or float-shaped.
  bool is_float = false;
  while (!at_end() && is_digit(peek())) advance();

  if (!at_end() && peek() == '.' && is_digit(peek(1))) {
    is_float = true;
    advance();  // '.'
    while (!at_end() && is_digit(peek())) advance();
  }

  if (!at_end() && (peek() == 'e' || peek() == 'E')) {
    // Exponent: e[+/-]?digits. Only consume it if a digit actually follows, so
    // that "1e" is two tokens rather than a malformed number swallowing 'e'.
    std::size_t mark = pos_;
    std::size_t look = 1;
    if (peek(look) == '+' || peek(look) == '-') ++look;
    if (is_digit(peek(look))) {
      is_float = true;
      advance();  // 'e'
      if (peek() == '+' || peek() == '-') advance();
      while (!at_end() && is_digit(peek())) advance();
    } else {
      (void)mark;
    }
  }

  std::string lexeme(reinterpret_cast<const char*>(data_ + start), pos_ - start);
  try {
    if (is_float) {
      tok.kind = TokenKind::kFloat;
      tok.float_value = std::stod(lexeme);
    } else {
      tok.kind = TokenKind::kInt;
      // stoll throws std::out_of_range on overflow; map that to a parse error.
      tok.int_value = static_cast<std::int64_t>(std::stoll(lexeme));
    }
  } catch (const std::exception&) {
    throw ParseError(ErrorCode::kBadLength, start,
                     "numeric literal out of range");
  }
  return tok;
}

void Lexer::append_utf8(std::string& out, std::uint32_t cp,
                        std::size_t err_offset) const {
  if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
    error(err_offset, "invalid Unicode code point");
  }
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

Token Lexer::lex_string() {
  Token tok;
  tok.offset = pos_;
  std::size_t open = pos_;
  advance();  // opening quote

  // Build the decoded payload manually, byte by byte, resolving escapes as we
  // go. Raw bytes (including >= 0x80) pass through verbatim.
  std::string out;
  for (;;) {
    if (at_end()) error(open, "unterminated string literal");
    std::uint8_t c = peek();
    if (c == '"') {
      advance();  // closing quote
      break;
    }
    if (c == '\n') error(pos_, "unterminated string literal (newline in string)");
    if (c != '\\') {
      out.push_back(static_cast<char>(c));
      advance();
      continue;
    }

    // Escape sequence.
    std::size_t esc = pos_;
    advance();  // backslash
    if (at_end()) error(esc, "unterminated escape sequence");
    std::uint8_t e = advance();
    switch (e) {
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case '0': out.push_back('\0'); break;
      case '\\': out.push_back('\\'); break;
      case '"': out.push_back('"'); break;
      case 'x': {
        // Exactly two hex digits -> one raw byte.
        if (!is_hex_digit(peek()) || !is_hex_digit(peek(1))) {
          error(esc, "\\x escape needs two hex digits");
        }
        int hi = hex_value(advance());
        int lo = hex_value(advance());
        out.push_back(static_cast<char>((hi << 4) | lo));
        break;
      }
      case 'u': {
        // \u{XXXX}: one or more hex digits inside braces -> UTF-8.
        if (peek() != '{') error(esc, "\\u escape needs '{'");
        advance();  // '{'
        std::uint32_t cp = 0;
        int count = 0;
        while (!at_end() && is_hex_digit(peek())) {
          cp = cp * 16u + static_cast<std::uint32_t>(hex_value(advance()));
          ++count;
          if (count > 6) error(esc, "\\u escape has too many digits");
        }
        if (count == 0) error(esc, "\\u escape needs at least one hex digit");
        if (at_end() || peek() != '}') error(esc, "\\u escape needs '}'");
        advance();  // '}'
        append_utf8(out, cp, esc);
        break;
      }
      default:
        error(esc, "unknown escape sequence");
    }
  }

  tok.kind = TokenKind::kString;
  tok.text = std::move(out);
  return tok;
}

}  // namespace cfgscript
}  // namespace bw
