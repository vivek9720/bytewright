#include "common/byte_writer.hpp"

namespace bw {
namespace common {

void ByteWriter::put_u8(std::uint8_t v) { buffer_.push_back(v); }

void ByteWriter::put_u16le(std::uint16_t v) {
  buffer_.push_back(static_cast<std::uint8_t>(v & 0xFF));
  buffer_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void ByteWriter::put_u32le(std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    buffer_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

void ByteWriter::put_u64le(std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    buffer_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

void ByteWriter::put_u16be(std::uint16_t v) {
  buffer_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  buffer_.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void ByteWriter::put_u32be(std::uint32_t v) {
  for (int i = 3; i >= 0; --i) {
    buffer_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

void ByteWriter::put_u64be(std::uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    buffer_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

void ByteWriter::put_varint(std::uint64_t v) {
  while (v >= 0x80) {
    buffer_.push_back(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
    v >>= 7;
  }
  buffer_.push_back(static_cast<std::uint8_t>(v));
}

void ByteWriter::put_svarint(std::int64_t v) {
  std::uint64_t zig = (static_cast<std::uint64_t>(v) << 1) ^
                      static_cast<std::uint64_t>(v >> 63);
  put_varint(zig);
}

void ByteWriter::put_bytes(const std::uint8_t* data, std::size_t n) {
  buffer_.insert(buffer_.end(), data, data + n);
}

void ByteWriter::put_bytes(const std::vector<std::uint8_t>& data) {
  buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void ByteWriter::put_string(const std::string& s) {
  buffer_.insert(buffer_.end(), s.begin(), s.end());
}

std::size_t ByteWriter::reserve_u32le() {
  std::size_t pos = buffer_.size();
  buffer_.insert(buffer_.end(), 4, 0);
  return pos;
}

void ByteWriter::patch_u32le(std::size_t pos, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    buffer_[pos + static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
  }
}

}  // namespace common
}  // namespace bw
