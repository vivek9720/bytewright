// bytewright - streamcodec frame reader implementation.
//
// See frame_reader.hpp for the framing policy. The header is decoded field by
// field with big-endian readers; the fixed header bytes are also borrowed as a
// contiguous span so the optional CRC-16 can be checked over exactly the bytes
// that precede it.
#include "streamcodec/frame_reader.hpp"

#include "common/status.hpp"

namespace bw {
namespace streamcodec {

using common::ByteReader;
using common::ErrorCode;
using common::ParseError;

void FrameReader::align_to_sync(bool first) {
  // The fast path: we are already sitting on a sync word.
  if (r_.remaining() >= 2) {
    const std::uint8_t* p = r_.base() + r_.offset();
    if (p[0] == ((kSync >> 8) & 0xFF) && p[1] == (kSync & 0xFF)) {
      return;
    }
  }

  // The first frame must begin with the sync word; a mismatch is unrecoverable.
  if (first) {
    throw ParseError(ErrorCode::kBadMagic, r_.offset(),
                     "first frame does not start with sync word");
  }

  // Later frames: scan forward a bounded number of bytes looking for the sync
  // word. This tolerates a little inter-frame corruption without unbounded work.
  std::size_t scanned = 0;
  while (r_.remaining() >= 2 && scanned < kResyncWindow) {
    const std::uint8_t* p = r_.base() + r_.offset();
    if (p[0] == ((kSync >> 8) & 0xFF) && p[1] == (kSync & 0xFF)) {
      return;
    }
    r_.skip(1);
    ++scanned;
  }

  // Could not realign: consume whatever is left so has_more() reports done.
  if (r_.remaining() < 2) {
    r_.skip(r_.remaining());
    return;
  }
  throw ParseError(ErrorCode::kBadMagic, r_.offset(),
                   "could not resynchronize to a frame within the scan window");
}

Frame FrameReader::next(bool first) {
  align_to_sync(first);

  // If alignment consumed the tail (resync gave up at EOF) there is no frame.
  if (r_.remaining() < 2) {
    throw ParseError(ErrorCode::kShortRead, r_.offset(),
                     "no frame remaining after sync alignment");
  }

  Frame f;
  f.header_offset = r_.offset();

  // Borrow the fixed header span up front for the CRC computation. read_raw
  // throws kShortRead if fewer than kHeaderFixedBytes remain, which is exactly
  // the truncated-header case we want surfaced as a hard error.
  const std::uint8_t* hdr = r_.read_raw(kHeaderFixedBytes);

  // Re-decode the borrowed span with a bounded reader so field extraction reads
  // naturally and stays in lockstep with the documented layout.
  ByteReader h(hdr, kHeaderFixedBytes);

  const std::uint16_t sync = h.read_u16be();
  if (sync != kSync) {
    // align_to_sync guarantees this, but assert the invariant defensively.
    throw ParseError(ErrorCode::kBadMagic, f.header_offset,
                     "frame sync mismatch after alignment");
  }

  f.version = h.read_u8();
  if (f.version != kVersion) {
    throw ParseError(ErrorCode::kBadVersion, f.header_offset,
                     "unsupported frame version");
  }

  const std::uint8_t type_byte = h.read_u8();
  switch (type_byte) {
    case 0: f.type = FrameType::kData;    break;
    case 1: f.type = FrameType::kControl; break;
    case 2: f.type = FrameType::kAck;     break;
    case 3: f.type = FrameType::kReset;   break;
    default:
      throw ParseError(ErrorCode::kBadType, f.header_offset,
                       "unknown frame type");
  }

  f.flags = h.read_u8();
  f.stream_id = h.read_u16be();
  f.msg_id = h.read_u32be();
  f.seq = h.read_u16be();
  f.frag_offset = h.read_u32be();
  f.payload_len = h.read_u16be();

  f.has_crc = (f.flags & kFlagHasCrc) != 0;
  if (f.has_crc) {
    // The CRC field sits immediately after the fixed header in the stream.
    f.header_crc = r_.read_u16be();
    const std::uint16_t computed = crc16(hdr, kHeaderFixedBytes);
    f.crc_ok = (computed == f.header_crc);
    // Lenient by design: a mismatch is recorded on the frame and decoding
    // continues. We never throw kBadChecksum in the default mode.
  }

  // Copy the payload. read_bytes throws kShortRead if the declared length runs
  // off the end of the buffer, which is the truncated-payload hard error.
  f.payload = r_.read_bytes(f.payload_len);

  return f;
}

}  // namespace streamcodec
}  // namespace bw
