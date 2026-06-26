#include "common/byte_reader.hpp"

#include <cstring>

namespace bw {
namespace common {

void ByteReader::seek(std::size_t pos) {
  if (pos > size_) {
    throw ParseError(ErrorCode::kBadLength, pos, "seek past end of buffer");
  }
  pos_ = pos;
}

void ByteReader::skip(std::size_t n) {
  require(n);
  pos_ += n;
}

void ByteReader::require(std::size_t n) const {
  if (n > remaining()) {
    throw ParseError(ErrorCode::kShortRead, pos_);
  }
}

std::uint8_t ByteReader::read_u8() {
  require(1);
  return data_[pos_++];
}

std::uint16_t ByteReader::read_u16le() {
  require(2);
  std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) |
                    (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8);
  pos_ += 2;
  return v;
}

std::uint32_t ByteReader::read_u32le() {
  require(4);
  std::uint32_t v = static_cast<std::uint32_t>(data_[pos_]) |
                    (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
                    (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
                    (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
  pos_ += 4;
  return v;
}

std::uint64_t ByteReader::read_u64le() {
  require(8);
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; --i) {
    v = (v << 8) | data_[pos_ + static_cast<std::size_t>(i)];
  }
  pos_ += 8;
  return v;
}

std::uint16_t ByteReader::read_u16be() {
  require(2);
  std::uint16_t v = static_cast<std::uint16_t>(data_[pos_] << 8) |
                    static_cast<std::uint16_t>(data_[pos_ + 1]);
  pos_ += 2;
  return v;
}

std::uint32_t ByteReader::read_u32be() {
  require(4);
  std::uint32_t v = (static_cast<std::uint32_t>(data_[pos_]) << 24) |
                    (static_cast<std::uint32_t>(data_[pos_ + 1]) << 16) |
                    (static_cast<std::uint32_t>(data_[pos_ + 2]) << 8) |
                    static_cast<std::uint32_t>(data_[pos_ + 3]);
  pos_ += 4;
  return v;
}

std::uint64_t ByteReader::read_u64be() {
  require(8);
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | data_[pos_ + static_cast<std::size_t>(i)];
  }
  pos_ += 8;
  return v;
}

std::uint64_t ByteReader::read_varint() {
  std::uint64_t result = 0;
  int shift = 0;
  std::size_t start = pos_;
  for (int byte_index = 0; byte_index < 10; ++byte_index) {
    std::uint8_t b = read_u8();
    result |= static_cast<std::uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      return result;
    }
    shift += 7;
  }
  throw ParseError(ErrorCode::kBadLength, start, "varint too long");
}

std::int64_t ByteReader::read_svarint() {
  std::uint64_t u = read_varint();
  return static_cast<std::int64_t>((u >> 1) ^ (~(u & 1) + 1));
}

std::vector<std::uint8_t> ByteReader::read_bytes(std::size_t n) {
  require(n);
  std::vector<std::uint8_t> out(data_ + pos_, data_ + pos_ + n);
  pos_ += n;
  return out;
}

std::string ByteReader::read_string(std::size_t n) {
  require(n);
  std::string out(reinterpret_cast<const char*>(data_ + pos_), n);
  pos_ += n;
  return out;
}

const std::uint8_t* ByteReader::read_raw(std::size_t n) {
  require(n);
  const std::uint8_t* p = data_ + pos_;
  pos_ += n;
  return p;
}

std::uint8_t ByteReader::peek_u8() const {
  require(1);
  return data_[pos_];
}

bool ByteReader::match(const std::uint8_t* expected, std::size_t n) const {
  if (n > remaining()) {
    return false;
  }
  return std::memcmp(data_ + pos_, expected, n) == 0;
}

}  // namespace common
}  // namespace bw
