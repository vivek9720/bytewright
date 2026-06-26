// bytewright - cfgscript public facade
//
// cfgscript parses a small, original C-like configuration/script language into
// an evaluated Document tree. This header is the entire public surface that the
// rest of the monorepo (notably tools/bwdump) depends on.
//
// Errors: parse() throws bw::common::ParseError on any lexical, syntactic, or
// semantic problem (unterminated string, unexpected token, runaway nesting,
// division by zero, unknown identifier, ...). The carried offset is the byte
// position of the failure.
//
// No file IO: an `include "path";` statement is recorded as data only. cfgscript
// never opens or reads the referenced file; resolving includes is left to a
// higher layer that is outside this library.
#ifndef BYTEWRIGHT_CFGSCRIPT_CFGSCRIPT_HPP
#define BYTEWRIGHT_CFGSCRIPT_CFGSCRIPT_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfgscript/model.hpp"

namespace bw {
namespace cfgscript {

// Parse and evaluate `size` bytes of cfgscript source into a Document.
Document parse(const std::uint8_t* data, std::size_t size);

// Convenience overload over a std::string of source text.
Document parse(const std::string& text);

// Render a Document as a human-readable, multi-line dump of the config tree.
std::string summarize(const Document& doc);

}  // namespace cfgscript
}  // namespace bw

#endif  // BYTEWRIGHT_CFGSCRIPT_CFGSCRIPT_HPP
