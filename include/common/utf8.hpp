// bytewright - UTF-8 validation and codepoint helpers
//
// String fields in the text and record formats are nominally UTF-8. These
// helpers provide strict validation (rejecting overlong encodings, surrogates,
// and out-of-range code points) plus decode/encode between byte strings and
// Unicode scalar values. Strict-by-default so malformed text is caught rather
// than silently mangled.
#ifndef BYTEWRIGHT_COMMON_UTF8_HPP
#define BYTEWRIGHT_COMMON_UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bw {
namespace common {
namespace utf8 {

// Largest valid Unicode scalar value.
constexpr std::uint32_t kMaxCodepoint = 0x10FFFF;

// Decode the scalar value starting at data[0..size). On success returns the
// number of bytes consumed (1..4) and writes the code point to `cp`. Returns 0
// on any malformed/overlong/surrogate/out-of-range sequence (cp untouched).
std::size_t decode(const std::uint8_t* data, std::size_t size,
                   std::uint32_t& cp) noexcept;

// True iff the entire buffer is well-formed UTF-8.
bool validate(const std::uint8_t* data, std::size_t size) noexcept;
bool validate(const std::string& s) noexcept;

// Count the scalar values in a valid buffer; returns false if invalid.
bool count_codepoints(const std::uint8_t* data, std::size_t size,
                      std::size_t& out) noexcept;

// Append the UTF-8 encoding of `cp` to `out`. Returns false (and appends
// nothing) if `cp` is a surrogate or above kMaxCodepoint.
bool encode(std::uint32_t cp, std::string& out);

// Decode an entire string to scalar values. Returns false on the first invalid
// sequence (partial results left in `out`).
bool to_codepoints(const std::string& s, std::vector<std::uint32_t>& out);

}  // namespace utf8
}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_UTF8_HPP
