// bytewright - minidb internal decoder declarations
//
// These are the stage functions open() chains together. They are exposed in a
// header (rather than file-local statics) so the unit tests can exercise each
// stage in isolation and so the multi-file split stays honest about its seams.
// Not part of the public facade.
#ifndef BYTEWRIGHT_MINIDB_DECODER_HPP
#define BYTEWRIGHT_MINIDB_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "common/byte_reader.hpp"
#include "minidb/format.hpp"
#include "minidb/model.hpp"

namespace bw {
namespace minidb {

// Stage 1: parse + validate the fixed 32-byte file header. Throws on bad magic,
// bad version, or an out-of-range page_size. The CRC is checked leniently: on
// mismatch `out.checksum_ok` is set false (and, only if `strict`, we throw).
FileHeader parse_header(common::ByteReader& reader, bool strict);

// Stage 2: walk the page array. Each page i lives at 32 + i*page_size. Pages
// that run past EOF are clamped/skipped (lenient). Populates db.pages and
// returns the raw page byte spans so later stages can read slot contents without
// re-deriving offsets. The returned vector is parallel to db.pages.
struct PageSpan {
  std::size_t file_offset = 0;  // absolute offset of the page in the buffer
  std::size_t length = 0;       // bytes actually available (<= page_size)
};
std::vector<PageSpan> parse_pages(const std::uint8_t* data, std::size_t size,
                                  const FileHeader& hdr, Database& db);

// Stage 3: decode the STRINGS page (if any) into db.string_table.
void parse_string_table(const std::uint8_t* data, std::size_t size,
                        const FileHeader& hdr,
                        const std::vector<PageSpan>& spans, Database& db);

// Decode a single record's bytes into a Record, resolving STRING_REF ids against
// the string table. Throws kBadType on an unknown field tag. Used both for the
// initial page-resident records and for journal-staged record bytes.
Record decode_record(const std::uint8_t* rec, std::size_t rec_len,
                     const std::vector<std::string>& string_table);

// Stage 4: replay the journal from hdr.journal_offset to EOF, mutating an
// in-memory store, then decode every committed record into db.records.
void replay_journal(const std::uint8_t* data, std::size_t size,
                    const FileHeader& hdr,
                    const std::vector<PageSpan>& spans, Database& db);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_DECODER_HPP
