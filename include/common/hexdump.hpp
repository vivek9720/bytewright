// bytewright - small formatting helpers shared by the CLI tools.
#ifndef BYTEWRIGHT_COMMON_HEXDUMP_HPP
#define BYTEWRIGHT_COMMON_HEXDUMP_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bw {
namespace common {

// Classic `offset  hex  ascii` dump, 16 bytes per row.
std::string hexdump(const std::uint8_t* data, std::size_t size);
std::string hexdump(const std::vector<std::uint8_t>& data);

// Render a single value as 0x-prefixed hex.
std::string to_hex(std::uint64_t value, int min_digits = 0);

// Escape a byte string into a printable, single-line form (control bytes and
// non-ASCII become \xHH). Useful for logging decoded string fields.
std::string escape(const std::string& s);

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_HEXDUMP_HPP
