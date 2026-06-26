// bytewright - sequential byte builder
//
// The mirror of ByteReader. Used by tests and the corpus-generation tooling to
// assemble well-formed inputs without hand-rolling byte arrays. Keeping the
// encoder beside the decoder also documents the wire formats in one place.
#ifndef BYTEWRIGHT_COMMON_BYTE_WRITER_HPP
#define BYTEWRIGHT_COMMON_BYTE_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bw {
namespace common {

class ByteWriter {
 public:
  ByteWriter() = default;

  std::size_t size() const noexcept { return buffer_.size(); }
  const std::vector<std::uint8_t>& bytes() const noexcept { return buffer_; }
  std::uint8_t* mutable_at(std::size_t pos) { return &buffer_[pos]; }

  void put_u8(std::uint8_t v);
  void put_u16le(std::uint16_t v);
  void put_u32le(std::uint32_t v);
  void put_u64le(std::uint64_t v);
  void put_u16be(std::uint16_t v);
  void put_u32be(std::uint32_t v);
  void put_u64be(std::uint64_t v);

  void put_varint(std::uint64_t v);
  void put_svarint(std::int64_t v);

  void put_bytes(const std::uint8_t* data, std::size_t n);
  void put_bytes(const std::vector<std::uint8_t>& data);
  void put_string(const std::string& s);

  // Reserve a 32-bit little-endian slot, returning its offset so the caller can
  // back-patch a length or checksum once the trailing content is known.
  std::size_t reserve_u32le();
  void patch_u32le(std::size_t pos, std::uint32_t v);

 private:
  std::vector<std::uint8_t> buffer_;
};

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_BYTE_WRITER_HPP
