// bytewright - streamcodec frame encoder
//
// The serializing counterpart to FrameReader. FrameEncoder turns a small,
// typed description of a frame into the exact big-endian byte layout described
// in frame.hpp, optionally appending the HAS_CRC header checksum computed with
// crc16() over the fixed header bytes. The bytes it emits are, by construction,
// the same bytes the existing reader/decoder accept.
//
// Two layers are offered:
//   * encode_frame / FrameSpec - emit a single frame of any type.
//   * encode_message - fragment a (possibly large) payload into a sequence of
//     DATA frames with correct frag_offset values and a trailing FIN, so the
//     reassembler reconstructs the original bytes. A payload that fits in one
//     frame is emitted as a single non-FRAG FIN frame.
//
// Big-endian throughout, C++17, no external dependencies. The encoder never
// throws; it only appends to a caller-owned ByteWriter (or returns a vector).
#ifndef BYTEWRIGHT_STREAMCODEC_ENCODER_HPP
#define BYTEWRIGHT_STREAMCODEC_ENCODER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/byte_writer.hpp"
#include "streamcodec/frame.hpp"

namespace bw {
namespace streamcodec {

// A complete description of one frame to serialize. The flag bits are taken
// verbatim except for HAS_CRC, which is driven by the `with_crc` toggle so the
// CRC field and its flag bit never disagree.
struct FrameSpec {
  FrameType type = FrameType::kData;
  std::uint8_t flags = 0;  // FIN/FRAG/COMPRESSED bits; HAS_CRC is added by with_crc
  std::uint16_t stream_id = 0;
  std::uint32_t msg_id = 0;
  std::uint16_t seq = 0;
  std::uint32_t frag_offset = 0;
  std::vector<std::uint8_t> payload;
  bool with_crc = false;  // when true, append the header CRC and set HAS_CRC
};

// The largest payload a single frame can carry. payload_len is a u16 on the
// wire, so a fragment is capped at 65535 bytes.
constexpr std::size_t kMaxFramePayload = 0xFFFF;

class FrameEncoder {
 public:
  // Append one frame, described by `spec`, to `w`. The flags written are
  // spec.flags with HAS_CRC forced on/off to match spec.with_crc. The payload
  // is truncated defensively to kMaxFramePayload (callers that fragment never
  // hit this; it only guards a hand-built oversized spec).
  static void encode_frame(common::ByteWriter& w, const FrameSpec& spec);

  // Convenience: encode a single frame into a fresh vector.
  static std::vector<std::uint8_t> encode_frame(const FrameSpec& spec);

  // Fragment `payload` for (stream_id, msg_id) into DATA frames and append them
  // to `w`. Each fragment carries at most `max_fragment` bytes (clamped to
  // [1, kMaxFramePayload]); FRAG is set when more than one fragment is needed,
  // and the final frame always carries FIN. `seq` numbers the frames starting
  // at `first_seq`, incrementing by one. When `compressed` is set the
  // COMPRESSED flag is marked on every fragment (metadata only). Returns the
  // number of frames emitted.
  //
  // A zero-length payload still emits exactly one (empty) FIN frame so the
  // message completes on the decode side.
  static std::size_t encode_message(common::ByteWriter& w,
                                    std::uint16_t stream_id,
                                    std::uint32_t msg_id,
                                    const std::vector<std::uint8_t>& payload,
                                    std::size_t max_fragment,
                                    std::uint16_t first_seq = 0,
                                    bool compressed = false,
                                    bool with_crc = false);
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_ENCODER_HPP
