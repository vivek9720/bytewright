#include "common/bit_reader.hpp"

namespace bw {
namespace common {

std::uint8_t BitReader::read_bit() {
  if (bit_pos_ >= size_bits_) {
    throw ParseError(ErrorCode::kShortRead, bit_pos_ / 8, "bit read past end");
  }
  const std::size_t byte = bit_pos_ / 8;
  const unsigned shift = 7u - static_cast<unsigned>(bit_pos_ % 8);
  ++bit_pos_;
  return static_cast<std::uint8_t>((data_[byte] >> shift) & 0x1u);
}

std::uint64_t BitReader::read_bits(unsigned count) {
  if (count == 0) {
    return 0;
  }
  if (count > 64) {
    throw ParseError(ErrorCode::kBadLength, bit_pos_ / 8,
                     "bit field wider than 64 bits");
  }
  if (count > bits_remaining()) {
    throw ParseError(ErrorCode::kShortRead, bit_pos_ / 8);
  }

  std::uint64_t value = 0;
  // Fast path: pull whole bytes when byte-aligned to avoid bit-by-bit work.
  while (count >= 8 && (bit_pos_ % 8) == 0) {
    value = (value << 8) | data_[bit_pos_ / 8];
    bit_pos_ += 8;
    count -= 8;
  }
  while (count > 0) {
    value = (value << 1) | read_bit();
    --count;
  }
  return value;
}

void BitReader::skip_bits(std::size_t count) {
  if (count > bits_remaining()) {
    throw ParseError(ErrorCode::kShortRead, bit_pos_ / 8);
  }
  bit_pos_ += count;
}

void BitReader::align() {
  const std::size_t rem = bit_pos_ % 8;
  if (rem != 0) {
    bit_pos_ += (8 - rem);
  }
}

}  // namespace common
}  // namespace bw
