// bytewright - streamcodec unit tests.
//
// Frames are assembled with common::ByteWriter (big-endian putters) via the
// small build_frame helper below, then fed through the public decode() facade.
#include <cstdint>
#include <vector>

#include "bw_test.hpp"
#include "common/byte_writer.hpp"
#include "common/status.hpp"
#include "streamcodec/frame.hpp"
#include "streamcodec/streamcodec.hpp"

namespace {

using bw::common::ByteWriter;
using bw::streamcodec::crc16;
using bw::streamcodec::FrameType;
using bw::streamcodec::kFlagCompressed;
using bw::streamcodec::kFlagFin;
using bw::streamcodec::kFlagFrag;
using bw::streamcodec::kFlagHasCrc;
using bw::streamcodec::kHeaderFixedBytes;
using bw::streamcodec::kSync;
using bw::streamcodec::kVersion;

// Append one frame to `w`. If `with_crc` is true a HAS_CRC field is emitted; the
// `crc_override` (when non-negative) lets a test write a deliberately wrong CRC.
void build_frame(ByteWriter& w, FrameType type, std::uint8_t flags,
                 std::uint16_t stream_id, std::uint32_t msg_id,
                 std::uint16_t seq, std::uint32_t frag_offset,
                 const std::vector<std::uint8_t>& payload, bool with_crc = false,
                 int crc_override = -1) {
  if (with_crc) {
    flags = static_cast<std::uint8_t>(flags | kFlagHasCrc);
  }

  // Encode the fixed header into a scratch buffer so we can CRC it, then splice.
  ByteWriter hdr;
  hdr.put_u16be(kSync);
  hdr.put_u8(kVersion);
  hdr.put_u8(static_cast<std::uint8_t>(type));
  hdr.put_u8(flags);
  hdr.put_u16be(stream_id);
  hdr.put_u32be(msg_id);
  hdr.put_u16be(seq);
  hdr.put_u32be(frag_offset);
  hdr.put_u16be(static_cast<std::uint16_t>(payload.size()));

  const std::vector<std::uint8_t>& hb = hdr.bytes();
  w.put_bytes(hb);

  if (with_crc) {
    std::uint16_t crc = crc16(hb.data(), hb.size());
    if (crc_override >= 0) {
      crc = static_cast<std::uint16_t>(crc_override);
    }
    w.put_u16be(crc);
  }

  w.put_bytes(payload);
}

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> in) {
  return std::vector<std::uint8_t>(in);
}

}  // namespace

BW_TEST(single_shot_data_decodes) {
  ByteWriter w;
  auto payload = bytes({0xDE, 0xAD, 0xBE, 0xEF});
  build_frame(w, FrameType::kData, kFlagFin, 7, 100, 0, 0, payload);

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.frames_seen, 1u);
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK_EQ(r.messages[0].stream_id, 7u);
  BW_CHECK_EQ(r.messages[0].msg_id, 100u);
  BW_CHECK(r.messages[0].data == payload);
  BW_CHECK(!r.messages[0].compressed);
}

BW_TEST(multi_fragment_out_of_order_reassembles) {
  // Message "ABCDEFGH" split into three fragments, the middle one delivered
  // last (out of order by frag_offset).
  ByteWriter w;
  auto f0 = bytes({'A', 'B', 'C'});       // [0,3)
  auto f1 = bytes({'D', 'E', 'F'});       // [3,6)
  auto f2 = bytes({'G', 'H'});            // [6,8) FIN

  build_frame(w, FrameType::kData, kFlagFrag, 1, 5, 0, 0, f0);
  // Deliver the final fragment (with FIN) before the middle one.
  build_frame(w, FrameType::kData,
              static_cast<std::uint8_t>(kFlagFrag | kFlagFin), 1, 5, 2, 6, f2);
  build_frame(w, FrameType::kData, kFlagFrag, 1, 5, 1, 3, f1);

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.frames_seen, 3u);
  BW_CHECK_EQ(r.messages.size(), 1u);
  auto expected = bytes({'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'});
  BW_CHECK(r.messages[0].data == expected);
}

BW_TEST(fin_completes_message) {
  // Without FIN the message stays in flight and is not emitted.
  ByteWriter w;
  build_frame(w, FrameType::kData, kFlagFrag, 2, 9, 0, 0, bytes({1, 2, 3}));
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.messages.size(), 0u);

  // Adding the FIN fragment completes it.
  build_frame(w, FrameType::kData,
              static_cast<std::uint8_t>(kFlagFrag | kFlagFin), 2, 9, 1, 3,
              bytes({4, 5}));
  auto r2 = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r2.messages.size(), 1u);
  BW_CHECK(r2.messages[0].data == bytes({1, 2, 3, 4, 5}));
}

BW_TEST(compressed_flag_recorded_but_bytes_untouched) {
  ByteWriter w;
  auto payload = bytes({0x01, 0x02, 0x03, 0x04, 0x05});
  build_frame(w, FrameType::kData,
              static_cast<std::uint8_t>(kFlagFin | kFlagCompressed), 3, 11, 0,
              0, payload);
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK(r.messages[0].compressed);
  // COMPRESSED is metadata only: the payload is delivered verbatim.
  BW_CHECK(r.messages[0].data == payload);
}

BW_TEST(bad_crc_still_decodes_with_flag) {
  ByteWriter w;
  auto payload = bytes({0xAA, 0xBB});
  // Force a wrong CRC; the frame must still decode and emit the message.
  build_frame(w, FrameType::kData, kFlagFin, 4, 12, 0, 0, payload,
              /*with_crc=*/true, /*crc_override=*/0x0000);
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK(r.messages[0].data == payload);
  BW_CHECK_EQ(r.header_crc_bad, 1u);
  BW_CHECK_EQ(r.header_crc_ok, 0u);
}

BW_TEST(good_crc_validates) {
  ByteWriter w;
  build_frame(w, FrameType::kData, kFlagFin, 4, 13, 0, 0, bytes({0x10}),
              /*with_crc=*/true);
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.header_crc_ok, 1u);
  BW_CHECK_EQ(r.header_crc_bad, 0u);
}

BW_TEST(reset_drops_partial_reassembly) {
  ByteWriter w;
  // Start a fragmented message but never FIN it.
  build_frame(w, FrameType::kData, kFlagFrag, 6, 20, 0, 0, bytes({9, 9, 9}));
  // RESET the stream: the partial buffer must be discarded.
  build_frame(w, FrameType::kReset, 0, 6, 0, 1, 0, {});
  // A later fragment with the same (stream,msg) at the original offset can no
  // longer complete the dropped message on its own.
  build_frame(w, FrameType::kData,
              static_cast<std::uint8_t>(kFlagFrag | kFlagFin), 6, 20, 2, 3,
              bytes({1, 1}));

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  // The pre-reset bytes are gone; [0,3) is never refilled, so nothing emits.
  BW_CHECK_EQ(r.messages.size(), 0u);
  BW_CHECK_EQ(r.reset_frames, 1u);
  // The reset is reflected in the per-stream stats.
  bool found = false;
  for (const auto& s : r.stats) {
    if (s.stream_id == 6) {
      found = true;
      BW_CHECK_EQ(s.resets, 1u);
    }
  }
  BW_CHECK(found);
}

BW_TEST(seq_gap_and_duplicate_counted) {
  ByteWriter w;
  // seq 0,1 in order, then jump to 5 (gap), then repeat 5 (duplicate).
  build_frame(w, FrameType::kData, kFlagFin, 8, 1, 0, 0, bytes({1}));
  build_frame(w, FrameType::kData, kFlagFin, 8, 2, 1, 0, bytes({2}));
  build_frame(w, FrameType::kData, kFlagFin, 8, 3, 5, 0, bytes({3}));
  build_frame(w, FrameType::kData, kFlagFin, 8, 4, 5, 0, bytes({4}));

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  bool found = false;
  for (const auto& s : r.stats) {
    if (s.stream_id == 8) {
      found = true;
      BW_CHECK_EQ(s.frames, 4u);
      BW_CHECK_EQ(s.gaps, 1u);
      BW_CHECK_EQ(s.duplicates, 1u);
    }
  }
  BW_CHECK(found);
}

BW_TEST(control_and_ack_recorded) {
  ByteWriter w;
  // CONTROL window-update: ref_msg_id=42, window=1024.
  ByteWriter cp;
  cp.put_u32be(42);
  cp.put_u32be(1024);
  build_frame(w, FrameType::kControl, 0, 3, 0, 0, 0, cp.bytes());
  build_frame(w, FrameType::kAck, 0, 3, 77, 1, 0, {});

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.control_frames, 1u);
  BW_CHECK_EQ(r.ack_frames, 1u);
  BW_CHECK_EQ(r.controls.size(), 1u);
  BW_CHECK_EQ(r.controls[0].ref_msg_id, 42u);
  BW_CHECK_EQ(r.controls[0].window, 1024u);
  BW_CHECK_EQ(r.acks.size(), 1u);
  BW_CHECK_EQ(r.acks[0].msg_id, 77u);
}

BW_TEST(first_frame_bad_sync_throws_bad_magic) {
  // A buffer that does not start with the sync word is a hard error.
  std::vector<std::uint8_t> junk = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  bool threw = false;
  try {
    bw::streamcodec::decode(junk.data(), junk.size());
  } catch (const bw::common::ParseError& e) {
    threw = true;
    BW_CHECK(e.code() == bw::common::ErrorCode::kBadMagic);
  }
  BW_CHECK(threw);
}

BW_TEST(truncated_payload_throws_short_read) {
  ByteWriter w;
  build_frame(w, FrameType::kData, kFlagFin, 1, 1, 0, 0,
              bytes({1, 2, 3, 4, 5, 6}));
  // Chop the last few payload bytes so payload_len overruns the buffer.
  std::vector<std::uint8_t> truncated(w.bytes().begin(), w.bytes().end() - 3);
  bool threw = false;
  try {
    bw::streamcodec::decode(truncated.data(), truncated.size());
  } catch (const bw::common::ParseError& e) {
    threw = true;
    BW_CHECK(e.code() == bw::common::ErrorCode::kShortRead);
  }
  BW_CHECK(threw);
}

BW_TEST(truncated_header_throws_short_read) {
  ByteWriter w;
  build_frame(w, FrameType::kData, kFlagFin, 1, 1, 0, 0, bytes({1}));
  // Keep only part of the fixed header.
  std::vector<std::uint8_t> truncated(w.bytes().begin(),
                                      w.bytes().begin() + (kHeaderFixedBytes - 4));
  BW_CHECK_THROWS(
      bw::streamcodec::decode(truncated.data(), truncated.size()));
}

BW_TEST(multi_stream_interleaving) {
  ByteWriter w;
  // Two independent single-shot messages on different streams, interleaved.
  build_frame(w, FrameType::kData, kFlagFin, 10, 1, 0, 0, bytes({0xA0}));
  build_frame(w, FrameType::kData, kFlagFin, 20, 1, 0, 0, bytes({0xB0}));
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.messages.size(), 2u);
  BW_CHECK_EQ(r.stats.size(), 2u);
}

BW_TEST(empty_input_is_empty_stream) {
  auto r = bw::streamcodec::decode(nullptr, 0);
  BW_CHECK_EQ(r.frames_seen, 0u);
  BW_CHECK_EQ(r.messages.size(), 0u);
}

BW_TEST(summarize_is_nonempty) {
  ByteWriter w;
  build_frame(w, FrameType::kData, kFlagFin, 1, 1, 0, 0, bytes({1, 2, 3}));
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK(bw::streamcodec::summarize(r).size() > 0);
}

BW_TEST_MAIN()
