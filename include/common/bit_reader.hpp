// bytewright - MSB-first bit cursor
//
// Several container formats carry sub-byte fields (flag groups, packed small
// integers). BitReader is the shared primitive for reading them. Bits are
// consumed most-significant-first within each byte, which is the convention the
// bit-packed sections of these formats use. Like ByteReader it is a non-owning
// view that throws common::ParseError(kShortRead) when it runs past the end.
#ifndef BYTEWRIGHT_COMMON_BIT_READER_HPP
#define BYTEWRIGHT_COMMON_BIT_READER_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace bw {
namespace common {

class BitReader {
 public:
  BitReader(const std::uint8_t* data, std::size_t size)
      : data_(data), size_bits_(size * 8), bit_pos_(0) {}

  std::size_t bit_offset() const noexcept { return bit_pos_; }
  std::size_t bits_remaining() const noexcept {
    return bit_pos_ <= size_bits_ ? size_bits_ - bit_pos_ : 0;
  }
  bool eof() const noexcept { return bit_pos_ >= size_bits_; }

  // Read a single bit (0/1). Throws kShortRead at end of input.
  std::uint8_t read_bit();

  // Read `count` (0..64) bits MSB-first into the low bits of the result.
  std::uint64_t read_bits(unsigned count);

  // Skip `count` bits without materializing them.
  void skip_bits(std::size_t count);

  // Advance to the next byte boundary (no-op if already aligned).
  void align();

 private:
  const std::uint8_t* data_;
  std::size_t size_bits_;
  std::size_t bit_pos_;
};

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_BIT_READER_HPP
