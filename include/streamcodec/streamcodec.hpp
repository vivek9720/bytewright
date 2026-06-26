// bytewright - streamcodec public facade
//
// streamcodec decodes a back-to-back stream of frames (sync word 0x53C7),
// reassembles fragmented messages per (stream_id, msg_id), and tracks rolling
// per-stream sequence state plus control/ack traffic. This header is the entire
// public surface tools/bwdump depends on: decode a buffer into a DecodeResult,
// or render a DecodeResult as text.
//
// Errors: decode() throws bw::common::ParseError only on HARD framing problems
// (first-frame bad sync, bad version, truncation/short read). Per-frame CRC
// mismatches are NOT hard errors in the default mode: decode() computes and
// compares the header CRC, records the result, and keeps going, so a fuzzer
// feeding near-valid bytes still reaches the reassembly and state paths.
#ifndef BYTEWRIGHT_STREAMCODEC_STREAMCODEC_HPP
#define BYTEWRIGHT_STREAMCODEC_STREAMCODEC_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "streamcodec/reassembler.hpp"
#include "streamcodec/stream_state.hpp"

namespace bw {
namespace streamcodec {

// A CONTROL frame's small parsed payload (a window-update: msg_id + window).
struct ControlRecord {
  std::uint16_t stream_id = 0;
  std::uint32_t ref_msg_id = 0;  // message the update refers to
  std::uint32_t window = 0;      // advertised window size
};

// An ACK frame: the stream and the message id being acknowledged.
struct AckRecord {
  std::uint16_t stream_id = 0;
  std::uint32_t msg_id = 0;
};

// The fully decoded result: completed messages in order of completion, plus the
// per-stream stats and the top-level counters.
struct DecodeResult {
  std::vector<Message> messages;
  std::vector<StreamStats> stats;
  std::vector<ControlRecord> controls;
  std::vector<AckRecord> acks;

  std::uint64_t frames_seen = 0;
  std::uint64_t control_frames = 0;
  std::uint64_t ack_frames = 0;
  std::uint64_t reset_frames = 0;
  std::uint64_t header_crc_ok = 0;    // frames whose header CRC validated
  std::uint64_t header_crc_bad = 0;   // frames whose header CRC mismatched
};

// Decode a frame stream. Throws bw::common::ParseError on hard framing errors.
DecodeResult decode(const std::uint8_t* data, std::size_t size);

// Human-readable, multi-line dump of a DecodeResult.
std::string summarize(const DecodeResult& r);

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_STREAMCODEC_HPP
