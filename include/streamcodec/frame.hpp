// bytewright - streamcodec frame model
//
// A streamcodec stream is a back-to-back sequence of self-delimiting frames.
// This header describes the on-wire frame header and the decoded in-memory
// frame the reader produces. Every multi-byte header field is big-endian.
//
// Frame header layout (all integers big-endian):
//   sync        u16   = 0x53C7 (kSync). First-frame mismatch is a hard error.
//   version     u8    = 1 (kVersion) else kBadVersion.
//   type        u8    DATA(0) | CONTROL(1) | ACK(2) | RESET(3)
//   flags       u8    bit0 FIN | bit1 FRAG | bit2 COMPRESSED | bit3 HAS_CRC
//   stream_id   u16
//   msg_id      u32
//   seq         u16   per-stream rolling frame counter (gap/dup tracking)
//   frag_offset u32   byte offset of this payload within the message
//   payload_len u16
//   header_crc  u16   present iff HAS_CRC; CRC-16 over the header bytes that
//                     precede it. Lenient: a mismatch sets a flag, never aborts.
//   payload     payload_len bytes
#ifndef BYTEWRIGHT_STREAMCODEC_FRAME_HPP
#define BYTEWRIGHT_STREAMCODEC_FRAME_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bw {
namespace streamcodec {

// On-wire constants.
constexpr std::uint16_t kSync = 0x53C7;
constexpr std::uint8_t kVersion = 1;

// Frame type tag (the header `type` byte).
enum class FrameType : std::uint8_t {
  kData = 0,
  kControl = 1,
  kAck = 2,
  kReset = 3,
};

const char* frame_type_name(FrameType type) noexcept;

// Frame flag bits (the header `flags` byte).
enum FrameFlag : std::uint8_t {
  kFlagFin = 0x01,         // last fragment of the message
  kFlagFrag = 0x02,        // payload is a fragment placed at frag_offset
  kFlagCompressed = 0x04,  // metadata only - payload is NOT decompressed
  kFlagHasCrc = 0x08,      // a header_crc field follows the fixed header
};

// Number of fixed header bytes that precede the optional CRC field. The CRC,
// when present, is computed over exactly these bytes.
constexpr std::size_t kHeaderFixedBytes = 19;

// A fully parsed frame: the decoded header plus a copy of its payload bytes.
struct Frame {
  FrameType type{};
  std::uint8_t version = 0;
  std::uint8_t flags = 0;
  std::uint16_t stream_id = 0;
  std::uint32_t msg_id = 0;
  std::uint16_t seq = 0;
  std::uint32_t frag_offset = 0;
  std::uint16_t payload_len = 0;

  bool has_crc = false;
  std::uint16_t header_crc = 0;  // stored CRC (only meaningful if has_crc)
  bool crc_ok = false;           // computed == stored (only if has_crc)

  std::size_t header_offset = 0;  // absolute offset of this frame's sync
  std::vector<std::uint8_t> payload;

  bool fin() const noexcept { return (flags & kFlagFin) != 0; }
  bool frag() const noexcept { return (flags & kFlagFrag) != 0; }
  bool compressed() const noexcept { return (flags & kFlagCompressed) != 0; }
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout). This
// is the checksum streamcodec uses for the optional per-frame header_crc. It is
// independent of common::crc32, which is a 32-bit reflected variant.
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_FRAME_HPP
