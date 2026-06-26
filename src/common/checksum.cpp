#include "common/checksum.hpp"

namespace bw {
namespace common {
namespace {

// Lazily-built CRC-32 lookup table for the reflected 0xEDB88320 polynomial.
// Built once on first use; deterministic and thread-safe under C++11 static
// initialization rules.
struct Crc32Table {
  std::uint32_t entries[256];
  Crc32Table() {
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      entries[n] = c;
    }
  }
};

const Crc32Table& crc_table() {
  static const Crc32Table table;
  return table;
}

constexpr std::uint32_t kAdlerMod = 65521u;

}  // namespace

std::uint32_t crc32_init() noexcept { return 0xFFFFFFFFu; }

std::uint32_t crc32_update(std::uint32_t state, const std::uint8_t* data,
                           std::size_t size) noexcept {
  const Crc32Table& table = crc_table();
  for (std::size_t i = 0; i < size; ++i) {
    state = table.entries[(state ^ data[i]) & 0xFFu] ^ (state >> 8);
  }
  return state;
}

std::uint32_t crc32_final(std::uint32_t state) noexcept {
  return state ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
  return crc32_final(crc32_update(crc32_init(), data, size));
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (std::size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % kAdlerMod;
    b = (b + a) % kAdlerMod;
  }
  return (b << 16) | a;
}

}  // namespace common
}  // namespace bw
