// bytewright - sequential byte cursor
//
// ByteReader is the shared primitive that every decoder uses to walk an input
// buffer. It does not own the bytes; it is a non-owning view with a cursor.
// The fixed-width and varint readers throw common::ParseError(kShortRead) when
// they would run off the end, so callers can write straight-line decode logic
// and let the structured error model handle truncation.
#ifndef BYTEWRIGHT_COMMON_BYTE_READER_HPP
#define BYTEWRIGHT_COMMON_BYTE_READER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/status.hpp"

namespace bw {
namespace common {

class ByteReader {
 public:
  ByteReader(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size), pos_(0) {}

  std::size_t offset() const noexcept { return pos_; }
  std::size_t size() const noexcept { return size_; }
  std::size_t remaining() const noexcept {
    return pos_ <= size_ ? size_ - pos_ : 0;
  }
  bool eof() const noexcept { return pos_ >= size_; }
  const std::uint8_t* base() const noexcept { return data_; }

  // Move the cursor to an absolute position. A position equal to size() is
  // legal (it denotes end-of-input); anything beyond is a length error.
  void seek(std::size_t pos);
  void skip(std::size_t n);

  // Throws kShortRead unless at least n bytes remain from the current cursor.
  void require(std::size_t n) const;

  std::uint8_t read_u8();
  std::uint16_t read_u16le();
  std::uint32_t read_u32le();
  std::uint64_t read_u64le();
  std::uint16_t read_u16be();
  std::uint32_t read_u32be();
  std::uint64_t read_u64be();

  // Unsigned LEB128. Rejects encodings longer than 10 bytes (the maximum a
  // 64-bit value needs) with kBadLength.
  std::uint64_t read_varint();
  // Zig-zag signed varint, layered on read_varint().
  std::int64_t read_svarint();

  // Copying readers. read_bytes/read_string allocate and advance the cursor.
  std::vector<std::uint8_t> read_bytes(std::size_t n);
  std::string read_string(std::size_t n);

  // Borrowing reader: returns a pointer into the underlying buffer valid for n
  // bytes and advances the cursor. The returned pointer is only valid for as
  // long as the backing storage outlives this reader.
  const std::uint8_t* read_raw(std::size_t n);

  // Peek helpers do not move the cursor.
  std::uint8_t peek_u8() const;
  bool match(const std::uint8_t* expected, std::size_t n) const;

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_;
};

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_BYTE_READER_HPP
