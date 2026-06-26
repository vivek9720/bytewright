// bytewright - streamcodec frame encoder implementation.
//
// The byte layout mirrors frame.hpp exactly: a fixed 19-byte big-endian header
// (sync, version, type, flags, stream_id, msg_id, seq, frag_offset, payload_len)
// optionally followed by a u16 header CRC, then the payload. The CRC, when
// requested, is computed over precisely the fixed header bytes - the same span
// the reader checks - so a round trip through decode() validates clean.
#include "streamcodec/encoder.hpp"

#include <algorithm>

#include "streamcodec/frame.hpp"

namespace bw {
namespace streamcodec {

namespace {

// Write the fixed header (kHeaderFixedBytes bytes) for `spec` into `hdr`, using
// the already-resolved `flags`. Kept separate so encode_frame can CRC exactly
// these bytes before splicing them into the output.
void write_fixed_header(common::ByteWriter& hdr, const FrameSpec& spec,
                        std::uint8_t flags, std::uint16_t payload_len) {
  hdr.put_u16be(kSync);
  hdr.put_u8(kVersion);
  hdr.put_u8(static_cast<std::uint8_t>(spec.type));
  hdr.put_u8(flags);
  hdr.put_u16be(spec.stream_id);
  hdr.put_u32be(spec.msg_id);
  hdr.put_u16be(spec.seq);
  hdr.put_u32be(spec.frag_offset);
  hdr.put_u16be(payload_len);
}

}  // namespace

void FrameEncoder::encode_frame(common::ByteWriter& w, const FrameSpec& spec) {
  // payload_len is a u16; clamp defensively so a hand-built oversized spec can
  // never desynchronize the stream by claiming more bytes than it writes.
  const std::size_t n = std::min(spec.payload.size(), kMaxFramePayload);
  const std::uint16_t payload_len = static_cast<std::uint16_t>(n);

  // Resolve the flags: keep the caller's FIN/FRAG/COMPRESSED bits and drive the
  // HAS_CRC bit purely from with_crc so the field and its flag agree.
  std::uint8_t flags = spec.flags;
  if (spec.with_crc) {
    flags = static_cast<std::uint8_t>(flags | kFlagHasCrc);
  } else {
    flags = static_cast<std::uint8_t>(flags & ~kFlagHasCrc);
  }

  // Build the fixed header in a scratch buffer so its CRC covers exactly those
  // bytes, then splice header (+CRC) and payload into the output.
  common::ByteWriter hdr;
  write_fixed_header(hdr, spec, flags, payload_len);
  const std::vector<std::uint8_t>& hb = hdr.bytes();

  w.put_bytes(hb);
  if (spec.with_crc) {
    w.put_u16be(crc16(hb.data(), hb.size()));
  }
  if (n != 0) {
    w.put_bytes(spec.payload.data(), n);
  }
}

std::vector<std::uint8_t> FrameEncoder::encode_frame(const FrameSpec& spec) {
  common::ByteWriter w;
  encode_frame(w, spec);
  return w.bytes();
}

std::size_t FrameEncoder::encode_message(
    common::ByteWriter& w, std::uint16_t stream_id, std::uint32_t msg_id,
    const std::vector<std::uint8_t>& payload, std::size_t max_fragment,
    std::uint16_t first_seq, bool compressed, bool with_crc) {
  // Clamp the fragment size into a sane, wire-representable range.
  if (max_fragment == 0) {
    max_fragment = 1;
  }
  max_fragment = std::min(max_fragment, kMaxFramePayload);

  const std::size_t total = payload.size();
  // Number of fragments: ceil(total / max_fragment), but always at least one so
  // a zero-length message still emits a single empty FIN frame.
  const std::size_t frag_count =
      total == 0 ? 1 : (total + max_fragment - 1) / max_fragment;

  // A single-fragment message is sent as a plain (non-FRAG) FIN frame; a
  // multi-fragment message sets FRAG on every piece and FIN on the last.
  const bool multi = frag_count > 1;

  std::size_t emitted = 0;
  std::size_t offset = 0;
  std::uint16_t seq = first_seq;
  for (std::size_t i = 0; i < frag_count; ++i) {
    const std::size_t chunk = std::min(max_fragment, total - offset);
    const bool last = (i + 1 == frag_count);

    FrameSpec spec;
    spec.type = FrameType::kData;
    spec.stream_id = stream_id;
    spec.msg_id = msg_id;
    spec.seq = seq;
    spec.frag_offset = static_cast<std::uint32_t>(offset);
    spec.with_crc = with_crc;

    std::uint8_t flags = 0;
    if (multi) {
      flags = static_cast<std::uint8_t>(flags | kFlagFrag);
    }
    if (last) {
      flags = static_cast<std::uint8_t>(flags | kFlagFin);
    }
    if (compressed) {
      flags = static_cast<std::uint8_t>(flags | kFlagCompressed);
    }
    spec.flags = flags;

    if (chunk != 0) {
      spec.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                          payload.begin() +
                              static_cast<std::ptrdiff_t>(offset + chunk));
    }

    encode_frame(w, spec);
    ++emitted;
    offset += chunk;
    ++seq;
  }

  return emitted;
}

}  // namespace streamcodec
}  // namespace bw
