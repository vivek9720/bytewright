// bytewright - cfgscript canonical serializer
//
// Renders an evaluated Document back into canonical cfgscript source text.
// Unlike summarize() (a human-readable dump prefixed with a node count), the
// serializer aims to emit text that re-parses into an equivalent Document:
// assignments terminated with `;`, blocks with `{ ... }` and optional string
// labels, arrays as `[ ... ]`, and object literals as `{ key = value; ... }`.
//
// Strings are re-escaped so that control bytes, quotes, backslashes and
// non-ASCII are emitted as valid escape sequences the lexer accepts. Two-space
// indentation is applied per nesting level so the output is stable and diffable.
#ifndef BYTEWRIGHT_CFGSCRIPT_SERIALIZER_HPP
#define BYTEWRIGHT_CFGSCRIPT_SERIALIZER_HPP

#include <cstddef>
#include <string>

#include "cfgscript/model.hpp"
#include "cfgscript/value.hpp"

namespace bw {
namespace cfgscript {

// Options controlling canonical rendering. Defaults produce two-space indents
// and multi-line arrays/objects only when they would otherwise be long.
struct SerializeOptions {
  // Number of spaces per indentation level.
  int indent_width = 2;

  // Arrays/objects with more than this many elements are split across lines;
  // smaller ones render inline. A value of 0 forces every aggregate multi-line.
  std::size_t inline_threshold = 4;
};

// Escape a raw byte string into the contents of a cfgscript string literal
// (without the surrounding quotes). Mirrors the escapes the lexer decodes:
// \\ \" \n \r \t and \xHH for other control/non-ASCII bytes.
std::string escape_string(const std::string& s);

// Render a single Value as canonical cfgscript expression text.
std::string serialize_value(const Value& value,
                            const SerializeOptions& opts = SerializeOptions{});

// Render an entire Document as canonical cfgscript source text.
std::string serialize(const Document& doc,
                      const SerializeOptions& opts = SerializeOptions{});

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_SERIALIZER_HPP
