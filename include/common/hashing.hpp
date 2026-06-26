// bytewright - small non-cryptographic hashes
//
// Used for content fingerprints and hash-keyed lookups inside the decoders
// (e.g. de-duplicating string-table entries, cheap integrity fingerprints).
// These are FNV-1a and a 64-bit integer mixer - fast, dependency-free, and
// stable across runs/platforms so any derived output is deterministic.
#ifndef BYTEWRIGHT_COMMON_HASHING_HPP
#define BYTEWRIGHT_COMMON_HASHING_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace bw {
namespace common {

// FNV-1a, the canonical offset basis / prime for each width.
std::uint32_t fnv1a_32(const std::uint8_t* data, std::size_t size) noexcept;
std::uint64_t fnv1a_64(const std::uint8_t* data, std::size_t size) noexcept;

std::uint32_t fnv1a_32(const std::string& s) noexcept;
std::uint64_t fnv1a_64(const std::string& s) noexcept;

// Incremental FNV-1a-32: seed with fnv1a_32_init(), feed chunks, read the state.
std::uint32_t fnv1a_32_init() noexcept;
std::uint32_t fnv1a_32_update(std::uint32_t state, const std::uint8_t* data,
                              std::size_t size) noexcept;

// A finalizing bit-mixer (splitmix64-style) for combining integer keys into a
// well-distributed hash, e.g. when keying a table by (page, slot).
std::uint64_t mix64(std::uint64_t x) noexcept;

// Combine two hashes into one (order-dependent), boost-style.
std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept;

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_HASHING_HPP
