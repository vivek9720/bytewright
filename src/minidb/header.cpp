// bytewright - minidb stage 1: file header parsing.
//
// The header is the single hard gate of the format. Magic and version mismatches
// abort the parse; a page_size outside [64,8192] or not a multiple of 16 is a
// length error. The header CRC, by contrast, is lenient: a mismatch only flips
// checksum_ok (we throw kBadChecksum only in strict mode). Everything downstream
// of this function is reachable on near-valid input.
#include "minidb/decoder.hpp"

#include <cstring>

#include "common/checksum.hpp"
#include "common/status.hpp"

namespace bw {
namespace minidb {

using common::ErrorCode;
using common::ParseError;

FileHeader parse_header(common::ByteReader& reader, bool strict) {
  // The whole fixed header must be present; a buffer shorter than 32 bytes can
  // never be a valid MDB1 store.
  reader.require(kHeaderSize);

  const std::uint8_t* base = reader.base();
  const std::size_t header_off = reader.offset();  // normally 0

  // Magic: this MAY throw, and is the only structural gate that does so up front.
  if (!reader.match(kMagic, sizeof(kMagic))) {
    throw ParseError(ErrorCode::kBadMagic, header_off, "expected MDB1 magic");
  }
  reader.skip(sizeof(kMagic));

  FileHeader hdr;
  hdr.version = reader.read_u16le();
  if (hdr.version != kSupportedVersion) {
    throw ParseError(ErrorCode::kBadVersion, header_off + 4,
                     "unsupported MDB1 version");
  }

  hdr.page_size = reader.read_u16le();
  if (hdr.page_size < kMinPageSize || hdr.page_size > kMaxPageSize ||
      (hdr.page_size % kPageSizeAlign) != 0) {
    throw ParseError(ErrorCode::kBadLength, header_off + 6,
                     "page_size out of range or misaligned");
  }

  hdr.page_count = reader.read_u32le();
  hdr.root_page = reader.read_u32le();
  hdr.string_table_page = reader.read_u32le();
  hdr.journal_offset = reader.read_u32le();
  hdr.flags = reader.read_u32le();
  hdr.header_crc32 = reader.read_u32le();

  // Lenient integrity check over the first 28 bytes. Compute, compare, record.
  const std::uint32_t computed =
      common::crc32(base + header_off, kHeaderCrcCovered);
  hdr.checksum_ok = (computed == hdr.header_crc32);
  if (!hdr.checksum_ok && strict) {
    throw ParseError(ErrorCode::kBadChecksum, header_off + 28,
                     "header CRC mismatch");
  }

  // Cursor is now positioned at the end of the 32-byte header.
  return hdr;
}

}  // namespace minidb
}  // namespace bw
