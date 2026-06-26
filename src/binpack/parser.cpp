// bytewright - binpack container parser
//
// "BPK1" file layout (all little-endian):
//
//   File header (12 bytes) + header_crc32 (4 bytes):
//     magic        "BPK1"   (4)
//     version      u16              must be 1
//     flags        u16              bit0 has_metadata, bit1 has_trailer_crc
//     section_count u16
//     reserved     u16              ignored
//     header_crc32 u32              CRC over the preceding 12 header bytes
//
//   Section table: section_count entries x 24 bytes:
//     type u16, id u16, flags u16, reserved u16,
//     offset u32, length u32, item_count u32, crc32 u32
//
//   Section bodies: at `offset`, `length` bytes of record stream. crc32 is over
//   those body bytes.
//
//   Trailer (only if flags bit1): last 4 bytes = crc32 over the whole file
//   except the trailer itself.
//
// Checksums are lenient by default: we compute, compare, store the result on the
// model, and keep parsing so the deep record paths stay reachable for a fuzzer.
// Options.strict turns any mismatch into a hard kBadChecksum (used by a test).
#include "binpack/binpack.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "binpack/record_parser.hpp"
#include "common/byte_reader.hpp"
#include "common/checksum.hpp"
#include "common/status.hpp"

namespace bw {
namespace binpack {

using common::ByteReader;
using common::ErrorCode;
using common::ParseError;

namespace {

constexpr std::uint8_t kMagic[4] = {'B', 'P', 'K', '1'};
constexpr std::size_t kHeaderBytes = 12;          // bytes covered by header_crc
constexpr std::size_t kHeaderTotal = 16;          // header + header_crc
constexpr std::size_t kSectionEntryBytes = 24;
constexpr std::size_t kTrailerBytes = 4;

// Compare a stored CRC against the freshly computed one. In strict mode a
// mismatch aborts; otherwise we just report the boolean so the caller can fold
// it into the container's overall checksum_ok and keep going.
bool verify_crc(std::uint32_t computed, std::uint32_t stored, bool strict,
                std::size_t offset, const char* what) {
  if (computed == stored) return true;
  if (strict) {
    throw ParseError(ErrorCode::kBadChecksum, offset, what);
  }
  return false;
}

}  // namespace

Container parse(const std::uint8_t* data, std::size_t size,
                const Options& opts) {
  if (data == nullptr && size != 0) {
    throw ParseError(ErrorCode::kStateViolation, 0, "null buffer");
  }

  ByteReader r(data, size);
  Container c;

  // --- File header -------------------------------------------------------
  // The magic check is allowed to be a hard error: seeds and the fuzz
  // dictionary supply the bytes, so this gates obviously-unrelated inputs out
  // cheaply without hiding the deeper code from the fuzzer.
  const std::uint8_t* magic = r.read_raw(4);
  for (int i = 0; i < 4; ++i) {
    if (magic[i] != kMagic[i]) {
      throw ParseError(ErrorCode::kBadMagic, 0, "not a BPK1 container");
    }
  }

  c.version = r.read_u16le();
  if (c.version != 1) {
    throw ParseError(ErrorCode::kBadVersion, 4, "unsupported binpack version");
  }
  c.flags = r.read_u16le();
  c.section_count = r.read_u16le();
  (void)r.read_u16le();  // reserved

  c.has_metadata = (c.flags & 0x01u) != 0;
  c.has_trailer_crc = (c.flags & 0x02u) != 0;

  const std::uint32_t header_crc_stored = r.read_u32le();
  // CRC is over the first 12 header bytes (everything before header_crc itself).
  // We needed `size >= 12`; read_raw above plus the u16/u32 reads guarantee it.
  const std::uint32_t header_crc_calc = common::crc32(data, kHeaderBytes);
  c.header_checksum_ok = verify_crc(header_crc_calc, header_crc_stored,
                                    opts.strict, 0, "header crc mismatch");

  // --- Section table -----------------------------------------------------
  struct Entry {
    SectionType type;
    std::uint16_t id;
    std::uint16_t flags;
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t item_count;
    std::uint32_t crc32_stored;
  };
  std::vector<Entry> entries;
  entries.reserve(c.section_count);

  // Make sure the whole declared section table actually fits in the file before
  // we start reading entries; this turns a malformed section_count into a clean
  // kBadLength instead of a series of kShortReads mid-table.
  const std::uint64_t table_bytes =
      static_cast<std::uint64_t>(c.section_count) * kSectionEntryBytes;
  if (kHeaderTotal + table_bytes > size) {
    throw ParseError(ErrorCode::kBadLength, kHeaderTotal,
                     "section table exceeds file size");
  }

  for (std::uint16_t i = 0; i < c.section_count; ++i) {
    Entry e;
    const std::uint16_t raw_type = r.read_u16le();
    // Map known types; an out-of-range type is a hard error (the table is the
    // structural backbone, so we don't try to recover past garbage here).
    switch (raw_type) {
      case 0: e.type = SectionType::kFree;     break;
      case 1: e.type = SectionType::kData;     break;
      case 2: e.type = SectionType::kMetadata; break;
      case 3: e.type = SectionType::kIndex;    break;
      case 4: e.type = SectionType::kStrings;  break;
      default:
        throw ParseError(ErrorCode::kBadType, r.offset() - 2,
                         "unknown section type");
    }
    e.id = r.read_u16le();
    e.flags = r.read_u16le();
    (void)r.read_u16le();  // reserved
    e.offset = r.read_u32le();
    e.length = r.read_u32le();
    e.item_count = r.read_u32le();
    e.crc32_stored = r.read_u32le();
    entries.push_back(e);
  }

  // --- Section bodies ----------------------------------------------------
  c.sections.reserve(entries.size());
  for (const Entry& e : entries) {
    Section s;
    s.type = e.type;
    s.id = e.id;
    s.flags = e.flags;
    s.offset = e.offset;
    s.length = e.length;
    s.item_count = e.item_count;
    s.crc32_stored = e.crc32_stored;

    // Validate that [offset, offset+length) lies inside the file. Use 64-bit
    // math so a crafted offset+length cannot wrap around.
    const std::uint64_t begin = e.offset;
    const std::uint64_t end = static_cast<std::uint64_t>(e.offset) + e.length;
    if (begin > size || end > size || end < begin) {
      throw ParseError(ErrorCode::kBadLength, e.offset,
                       "section body out of range");
    }

    const std::uint8_t* body_ptr = data + e.offset;
    const std::size_t body_len = static_cast<std::size_t>(e.length);

    // Per-section body CRC (lenient by default).
    const std::uint32_t body_crc = common::crc32(body_ptr, body_len);
    s.body_checksum_ok = verify_crc(body_crc, e.crc32_stored, opts.strict,
                                    e.offset, "section crc mismatch");

    // FREE sections are holes; we record them but do not decode a body.
    if (e.type != SectionType::kFree) {
      ByteReader body(body_ptr, body_len);
      RecordParser rp(opts.max_depth, e.offset);
      s.records = rp.parse_stream(body, /*depth=*/0);

      // Collect metadata from METADATA sections: each top-level record should
      // be a KEYVAL; we surface key + value node for convenient access.
      if (e.type == SectionType::kMetadata) {
        for (const Record& rec : s.records) {
          if (rec.tag == RecordTag::kKeyval && !rec.children.empty()) {
            c.metadata.emplace_back(rec.key, rec.children.front());
          }
        }
      }
    }

    c.sections.push_back(std::move(s));
  }

  // --- Trailer -----------------------------------------------------------
  if (c.has_trailer_crc) {
    if (size < kHeaderTotal + kTrailerBytes) {
      // A trailer was promised but there isn't room for one beyond the header.
      throw ParseError(ErrorCode::kTruncated, size,
                       "trailer crc flagged but file too small");
    }
    const std::size_t trailer_off = size - kTrailerBytes;
    ByteReader tr(data + trailer_off, kTrailerBytes);
    const std::uint32_t trailer_stored = tr.read_u32le();
    const std::uint32_t trailer_calc = common::crc32(data, trailer_off);
    c.trailer_checksum_ok =
        verify_crc(trailer_calc, trailer_stored, opts.strict, trailer_off,
                   "trailer crc mismatch");
  }

  // Fold all the per-piece results into the single headline flag.
  c.checksum_ok = c.header_checksum_ok && c.trailer_checksum_ok;
  for (const Section& s : c.sections) {
    c.checksum_ok = c.checksum_ok && s.body_checksum_ok;
  }

  return c;
}

Container parse(const std::uint8_t* data, std::size_t size) {
  return parse(data, size, Options{});
}

}  // namespace binpack
}  // namespace bw
