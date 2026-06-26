// bytewright - streamcodec frame reader
//
// Pulls one Frame at a time off a ByteReader positioned at a frame boundary.
// Framing policy:
//   * The FIRST frame's sync MUST equal kSync, else kBadMagic (hard error).
//   * For subsequent frames the reader attempts a bounded single-byte resync
//     scan: it advances at most kResyncWindow bytes looking for the sync word
//     before giving up. This keeps a stream with a small amount of inter-frame
//     corruption decodable without unbounded scanning.
//   * version != kVersion -> kBadVersion (hard error).
//   * A truncated header or payload surfaces as kShortRead from ByteReader.
//   * HAS_CRC mismatches are lenient: crc_ok is set false and decoding goes on.
#ifndef BYTEWRIGHT_STREAMCODEC_FRAME_READER_HPP
#define BYTEWRIGHT_STREAMCODEC_FRAME_READER_HPP

#include <cstddef>

#include "common/byte_reader.hpp"
#include "streamcodec/frame.hpp"

namespace bw {
namespace streamcodec {

class FrameReader {
 public:
  explicit FrameReader(common::ByteReader& reader) : r_(reader) {}

  // True while at least one more byte remains to start a frame.
  bool has_more() const { return !r_.eof(); }

  // Read the next frame. `first` selects the strict (kBadMagic) vs. resync sync
  // policy described above. Throws common::ParseError on hard framing errors.
  Frame next(bool first);

 private:
  // How far the resync scan will walk before declaring the stream unrecoverable.
  static constexpr std::size_t kResyncWindow = 4096;

  // Position the cursor on the next sync word. On the first frame a mismatch is
  // fatal; otherwise we scan up to kResyncWindow bytes.
  void align_to_sync(bool first);

  common::ByteReader& r_;
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_FRAME_READER_HPP
