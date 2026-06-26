// bytewright - streamcodec decode() driver.
//
// The frame loop: pull frames off the buffer one at a time, feed per-stream
// state, and dispatch on frame type. DATA frames go through the reassembler;
// CONTROL/ACK frames are recorded; RESET tears down a stream's reassembly and
// rolling counters. Hard framing errors propagate as common::ParseError; a
// per-frame CRC mismatch only flips a counter so the deep paths stay reachable.
#include "streamcodec/streamcodec.hpp"

#include <utility>

#include "common/byte_reader.hpp"
#include "common/status.hpp"
#include "streamcodec/frame.hpp"
#include "streamcodec/frame_reader.hpp"

namespace bw {
namespace streamcodec {

namespace {

// CONTROL payload is a tiny window-update: ref_msg_id u32 + window u32, both
// big-endian. Missing fields are tolerated and left at their default of zero so
// a short control payload never aborts the stream.
ControlRecord parse_control(const Frame& f) {
  ControlRecord c;
  c.stream_id = f.stream_id;
  common::ByteReader r(f.payload.data(), f.payload.size());
  if (r.remaining() >= 4) {
    c.ref_msg_id = r.read_u32be();
  }
  if (r.remaining() >= 4) {
    c.window = r.read_u32be();
  }
  return c;
}

}  // namespace

DecodeResult decode(const std::uint8_t* data, std::size_t size) {
  DecodeResult result;

  // An empty input is a valid, empty stream rather than an error.
  if (size == 0) {
    return result;
  }

  common::ByteReader reader(data, size);
  FrameReader frames(reader);
  Reassembler reassembler;
  StreamTable streams;

  bool first = true;
  while (frames.has_more()) {
    Frame f = frames.next(first);
    first = false;

    ++result.frames_seen;

    // Rolling per-stream seq accounting happens for every framed frame.
    streams.observe(f.stream_id, f.seq);

    // Tally header-CRC outcomes for frames that carried one.
    if (f.has_crc) {
      if (f.crc_ok) {
        ++result.header_crc_ok;
      } else {
        ++result.header_crc_bad;
      }
    }

    switch (f.type) {
      case FrameType::kData: {
        Message msg;
        if (reassembler.accept_data(f, msg)) {
          // Emitted in order of completion.
          result.messages.push_back(std::move(msg));
        }
        break;
      }

      case FrameType::kControl:
        ++result.control_frames;
        result.controls.push_back(parse_control(f));
        break;

      case FrameType::kAck: {
        ++result.ack_frames;
        AckRecord a;
        a.stream_id = f.stream_id;
        a.msg_id = f.msg_id;
        result.acks.push_back(a);
        break;
      }

      case FrameType::kReset:
        ++result.reset_frames;
        // Real lifecycle: drop partial reassembly buffers and clear the rolling
        // seq counter so a fresh sequence can start cleanly on this stream.
        reassembler.reset_stream(f.stream_id);
        streams.note_reset(f.stream_id);
        break;
    }
  }

  result.stats = streams.snapshot();
  return result;
}

}  // namespace streamcodec
}  // namespace bw
