// bytewright - streamcodec extension tests (encoder / transform / delivery /
// control). This translation unit deliberately omits BW_TEST_MAIN(); the
// existing test_streamcodec.cpp provides main() and the shared registry in
// bw_test.hpp collects cases from every linked TU.
#include <cstdint>
#include <vector>

#include "bw_test.hpp"
#include "common/byte_writer.hpp"
#include "streamcodec/control.hpp"
#include "streamcodec/delivery.hpp"
#include "streamcodec/encoder.hpp"
#include "streamcodec/reassembler.hpp"
#include "streamcodec/streamcodec.hpp"
#include "streamcodec/transform.hpp"

namespace {

using bw::common::ByteWriter;
using bw::streamcodec::DeliveryQueue;
using bw::streamcodec::FlowController;
using bw::streamcodec::FrameEncoder;
using bw::streamcodec::FrameSpec;
using bw::streamcodec::FrameType;
using bw::streamcodec::kFlagFin;
using bw::streamcodec::Message;
using bw::streamcodec::rle_compress;
using bw::streamcodec::rle_decompress;
using bw::streamcodec::RleStatus;
using bw::streamcodec::WindowAck;
using bw::streamcodec::WindowUpdate;

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> in) {
  return std::vector<std::uint8_t>(in);
}

// A Message handed to the delivery queue; only the routing fields matter here.
Message make_message(std::uint16_t stream_id, std::uint32_t msg_id) {
  Message m;
  m.stream_id = stream_id;
  m.msg_id = msg_id;
  m.data = bytes({static_cast<std::uint8_t>(msg_id)});
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Encoder: a single-shot frame produced by FrameEncoder decodes back through
// the existing public decode() to the original payload.
// ---------------------------------------------------------------------------
BW_TEST(encoder_single_shot_roundtrips) {
  FrameSpec spec;
  spec.type = FrameType::kData;
  spec.flags = kFlagFin;
  spec.stream_id = 7;
  spec.msg_id = 100;
  spec.payload = bytes({0xDE, 0xAD, 0xBE, 0xEF});
  spec.with_crc = true;  // exercise the header-CRC path

  ByteWriter w;
  FrameEncoder::encode_frame(w, spec);

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.frames_seen, 1u);
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK_EQ(r.messages[0].stream_id, 7u);
  BW_CHECK_EQ(r.messages[0].msg_id, 100u);
  BW_CHECK(r.messages[0].data == spec.payload);
  // CRC was written correctly, so the reader must validate it.
  BW_CHECK_EQ(r.header_crc_ok, 1u);
  BW_CHECK_EQ(r.header_crc_bad, 0u);
}

// ---------------------------------------------------------------------------
// Encoder: a large payload fragmented by encode_message reassembles via the
// existing decode() to exactly the original bytes.
// ---------------------------------------------------------------------------
BW_TEST(encoder_fragmented_message_roundtrips) {
  // 1000-byte payload with varied content, fragmented at 64 bytes/frame.
  std::vector<std::uint8_t> payload(1000);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
  }

  ByteWriter w;
  std::size_t frames = FrameEncoder::encode_message(
      w, /*stream_id=*/3, /*msg_id=*/55, payload, /*max_fragment=*/64);
  BW_CHECK_EQ(frames, 16u);  // ceil(1000/64) == 16

  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.frames_seen, 16u);
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK_EQ(r.messages[0].stream_id, 3u);
  BW_CHECK_EQ(r.messages[0].msg_id, 55u);
  BW_CHECK(r.messages[0].data == payload);
}

// A zero-length payload still yields one completed (empty) message.
BW_TEST(encoder_empty_message_completes) {
  ByteWriter w;
  std::size_t frames =
      FrameEncoder::encode_message(w, 1, 1, {}, /*max_fragment=*/64);
  BW_CHECK_EQ(frames, 1u);
  auto r = bw::streamcodec::decode(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.messages.size(), 1u);
  BW_CHECK_EQ(r.messages[0].data.size(), 0u);
}

// ---------------------------------------------------------------------------
// RLE transform: round-trip and bounded/rejected decompression.
// ---------------------------------------------------------------------------
BW_TEST(rle_roundtrips_runs_and_literals) {
  // Mix of long runs and non-repeating literals, including a >128 run that must
  // span multiple run tokens.
  std::vector<std::uint8_t> input;
  for (int i = 0; i < 300; ++i) input.push_back(0xAB);   // long run
  input.push_back(0x01);                                 // literal
  input.push_back(0x02);
  input.push_back(0x03);
  for (int i = 0; i < 5; ++i) input.push_back(0x7F);     // short run

  auto packed = rle_compress(input);
  BW_CHECK(packed.size() < input.size());  // it actually compressed

  std::vector<std::uint8_t> out;
  RleStatus st = rle_decompress(packed, /*max_output=*/4096, out);
  BW_CHECK(st == RleStatus::kOk);
  BW_CHECK(out == input);
}

// Round-trip of an all-literal (worst case) buffer is still exact.
BW_TEST(rle_roundtrips_all_literals) {
  std::vector<std::uint8_t> input;
  for (int i = 0; i < 200; ++i) {
    input.push_back(static_cast<std::uint8_t>(i * 3 + 1));  // no two adjacent equal
  }
  auto packed = rle_compress(input);
  std::vector<std::uint8_t> out;
  RleStatus st = rle_decompress(packed, 4096, out);
  BW_CHECK(st == RleStatus::kOk);
  BW_CHECK(out == input);
}

// A run token announcing 200 copies must be rejected by a small output cap,
// safely and without allocating 200 bytes.
BW_TEST(rle_oversized_decompress_is_capped) {
  // Build input that expands to 256 bytes: two run tokens of 128 copies each.
  std::vector<std::uint8_t> input(256, 0xCC);
  auto packed = rle_compress(input);

  std::vector<std::uint8_t> out;
  RleStatus st = rle_decompress(packed, /*max_output=*/100, out);
  BW_CHECK(st == RleStatus::kCapExceeded);
  // We stopped at or below the cap - never overran it.
  BW_CHECK(out.size() <= 100u);
}

// A malformed token (literal claiming more bytes than remain) is rejected with
// no out-of-bounds read.
BW_TEST(rle_malformed_literal_rejected) {
  // 0x7F => literal of 128 bytes, but no data follows it.
  std::vector<std::uint8_t> bad = {0x7F};
  std::vector<std::uint8_t> out;
  RleStatus st = rle_decompress(bad, 4096, out);
  BW_CHECK(st == RleStatus::kMalformed);

  // A run token with no value byte is likewise malformed.
  std::vector<std::uint8_t> bad_run = {0x80};  // run of 1, missing value
  out.clear();
  st = rle_decompress(bad_run, 4096, out);
  BW_CHECK(st == RleStatus::kMalformed);
}

// ---------------------------------------------------------------------------
// Delivery queue: reorders out-of-order msg_ids per stream and reports a gap.
// ---------------------------------------------------------------------------
BW_TEST(delivery_reorders_out_of_order) {
  DeliveryQueue q(/*window=*/16);

  // First arrival (id 0) establishes the start and releases immediately.
  auto out = q.offer(make_message(1, 0));
  BW_CHECK_EQ(out.size(), 1u);
  BW_CHECK_EQ(out[0].msg_id, 0u);

  // id 2 arrives before id 1: it must be buffered, releasing nothing yet.
  out = q.offer(make_message(1, 2));
  BW_CHECK_EQ(out.size(), 0u);
  BW_CHECK_EQ(q.buffered(), 1u);

  // id 1 arrives: it releases, then drains the buffered id 2 behind it.
  out = q.offer(make_message(1, 1));
  BW_CHECK_EQ(out.size(), 2u);
  BW_CHECK_EQ(out[0].msg_id, 1u);
  BW_CHECK_EQ(out[1].msg_id, 2u);
  BW_CHECK_EQ(q.buffered(), 0u);
}

// A late duplicate (id below the cursor) is dropped and counted.
BW_TEST(delivery_drops_duplicate) {
  DeliveryQueue q;
  q.offer(make_message(5, 10));  // start at 10, releases
  q.offer(make_message(5, 11));  // releases
  auto out = q.offer(make_message(5, 10));  // late duplicate
  BW_CHECK_EQ(out.size(), 0u);
  BW_CHECK_EQ(q.duplicates(), 1u);
}

// A full window forces the queue to skip past a missing id, reporting a gap and
// releasing the backlog.
BW_TEST(delivery_window_full_reports_gap) {
  DeliveryQueue q(/*window=*/2);

  // Establish the cursor at id 0 (releases immediately).
  auto out = q.offer(make_message(2, 0));
  BW_CHECK_EQ(out.size(), 1u);

  // ids 2 and 3 buffer (id 1 is missing); the window now holds 2.
  out = q.offer(make_message(2, 2));
  BW_CHECK_EQ(out.size(), 0u);
  out = q.offer(make_message(2, 3));
  BW_CHECK_EQ(out.size(), 0u);
  BW_CHECK_EQ(q.buffered(), 2u);

  // id 5 overflows the window: the missing id 1 is skipped (a gap), so the
  // cursor jumps to the lowest buffered id and drains the contiguous run 2,3.
  out = q.offer(make_message(2, 5));
  BW_CHECK(q.gaps() >= 1u);
  // 2 and 3 are contiguous and get released; 5 stays buffered behind a gap.
  bool saw2 = false, saw3 = false;
  for (const auto& m : out) {
    if (m.msg_id == 2) saw2 = true;
    if (m.msg_id == 3) saw3 = true;
  }
  BW_CHECK(saw2);
  BW_CHECK(saw3);
  BW_CHECK_EQ(q.buffered(), 1u);  // only id 5 remains
}

// ---------------------------------------------------------------------------
// Control flow: a stream blocks when its window is exhausted and unblocks when
// a window-update raises the ceiling.
// ---------------------------------------------------------------------------
BW_TEST(control_blocks_when_window_exhausted) {
  FlowController fc;

  // An unknown stream is blocked (nothing advertised).
  BW_CHECK(fc.blocked(1));

  // Advertise room for 100 bytes from offset 0.
  WindowUpdate u;
  u.stream_id = 1;
  u.ack_point = 0;
  u.window = 100;
  auto st = fc.on_window_update(u);
  BW_CHECK_EQ(st.advertised, 100u);
  BW_CHECK(!st.blocked);
  BW_CHECK_EQ(st.available(), 100u);

  // Send 60 bytes: admitted in full, still room.
  BW_CHECK_EQ(fc.try_send(1, 60), 60u);
  BW_CHECK(!fc.blocked(1));

  // Try to send 60 more: only 40 fit, and the stream is now blocked.
  BW_CHECK_EQ(fc.try_send(1, 60), 40u);
  BW_CHECK(fc.blocked(1));
  BW_CHECK_EQ(fc.state(1).in_flight(), 100u);  // nothing acked yet

  // A new window-update extends the ceiling: the stream unblocks.
  WindowUpdate u2;
  u2.stream_id = 1;
  u2.ack_point = 0;
  u2.window = 150;  // ceiling rises 100 -> 150
  st = fc.on_window_update(u2);
  BW_CHECK_EQ(st.advertised, 150u);
  BW_CHECK(!st.blocked);
  BW_CHECK_EQ(st.available(), 50u);

  // Acking 100 bytes drops in_flight to zero without changing the ceiling.
  WindowAck a;
  a.stream_id = 1;
  a.acked_bytes = 100;
  st = fc.on_ack(a);
  BW_CHECK_EQ(st.acked, 100u);
  BW_CHECK_EQ(st.in_flight(), 0u);
  BW_CHECK(!st.blocked);
}

// A stale (smaller) window-update never shrinks the advertised ceiling.
BW_TEST(control_ignores_stale_window_update) {
  FlowController fc;
  WindowUpdate big;
  big.stream_id = 9;
  big.ack_point = 0;
  big.window = 500;
  fc.on_window_update(big);

  WindowUpdate stale;
  stale.stream_id = 9;
  stale.ack_point = 0;
  stale.window = 100;  // smaller ceiling, must be ignored
  auto st = fc.on_window_update(stale);
  BW_CHECK_EQ(st.advertised, 500u);
}
