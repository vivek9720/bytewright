// bytewright - integrity primitives
//
// CRC-32 (IEEE/zlib polynomial, reflected) and Adler-32. Both are used by the
// container formats to validate sections and trailers. Implementations are
// table-driven and dependency-free.
#ifndef BYTEWRIGHT_COMMON_CHECKSUM_HPP
#define BYTEWRIGHT_COMMON_CHECKSUM_HPP

#include <cstddef>
#include <cstdint>

namespace bw {
namespace common {

// One-shot CRC-32 over a buffer.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept;

// Incremental CRC-32. Seed the first call with crc32_init(), feed chunks, and
// finish with crc32_final().
std::uint32_t crc32_init() noexcept;
std::uint32_t crc32_update(std::uint32_t state, const std::uint8_t* data,
                           std::size_t size) noexcept;
std::uint32_t crc32_final(std::uint32_t state) noexcept;

// One-shot Adler-32.
std::uint32_t adler32(const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_CHECKSUM_HPP
