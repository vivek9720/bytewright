#include "common/hashing.hpp"

namespace bw {
namespace common {
namespace {

constexpr std::uint32_t kFnv32Offset = 0x811C9DC5u;
constexpr std::uint32_t kFnv32Prime = 0x01000193u;
constexpr std::uint64_t kFnv64Offset = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnv64Prime = 0x00000100000001B3ull;

}  // namespace

std::uint32_t fnv1a_32_init() noexcept { return kFnv32Offset; }

std::uint32_t fnv1a_32_update(std::uint32_t state, const std::uint8_t* data,
                              std::size_t size) noexcept {
  for (std::size_t i = 0; i < size; ++i) {
    state ^= data[i];
    state *= kFnv32Prime;
  }
  return state;
}

std::uint32_t fnv1a_32(const std::uint8_t* data, std::size_t size) noexcept {
  return fnv1a_32_update(kFnv32Offset, data, size);
}

std::uint64_t fnv1a_64(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint64_t state = kFnv64Offset;
  for (std::size_t i = 0; i < size; ++i) {
    state ^= data[i];
    state *= kFnv64Prime;
  }
  return state;
}

std::uint32_t fnv1a_32(const std::string& s) noexcept {
  return fnv1a_32(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::uint64_t fnv1a_64(const std::string& s) noexcept {
  return fnv1a_64(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::uint64_t mix64(std::uint64_t x) noexcept {
  // splitmix64 finalizer - good avalanche for sequential integer keys.
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept {
  return seed ^ (mix64(value) + 0x9E3779B97F4A7C15ull + (seed << 6) +
                 (seed >> 2));
}

}  // namespace common
}  // namespace bw
