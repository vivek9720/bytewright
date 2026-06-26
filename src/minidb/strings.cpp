// bytewright - minidb stage 3: string table decoding.
//
// The string table lives on the page named by header.string_table_page (which
// must be a STRINGS page). Its body, immediately after the 8-byte page header,
// is `entry_count` (varint) followed by that many (varint len + bytes) entries.
// The entry index is its id; STRING_REF fields resolve against this vector.
//
// All reads go through a bounded ByteReader scoped to the page bytes, so a
// malformed count or length yields a structured kShortRead rather than a wild
// read. A truncated string page is tolerated: we keep whatever entries decoded
// cleanly and stop at the first short read.
#include "minidb/decoder.hpp"

#include "common/byte_reader.hpp"
#include "common/status.hpp"

namespace bw {
namespace minidb {

void parse_string_table(const std::uint8_t* data, std::size_t /*size*/,
                        const FileHeader& hdr,
                        const std::vector<PageSpan>& spans, Database& db) {
  if (hdr.string_table_page == kNoStringTable) {
    return;
  }
  // The named page must actually be one of the pages we walked.
  if (hdr.string_table_page >= spans.size() ||
      hdr.string_table_page >= db.pages.size()) {
    return;  // dangling pointer to a page beyond EOF: no table, not fatal
  }

  const Page& page = db.pages[hdr.string_table_page];
  if (page.type != PageType::kStrings) {
    return;  // not a strings page; leave the table empty
  }

  const PageSpan& span = spans[hdr.string_table_page];
  if (span.length <= kPageHeaderSize) {
    return;
  }

  // Body reader: scoped to this page's bytes, starting just past the page header.
  const std::uint8_t* body = data + span.file_offset + kPageHeaderSize;
  const std::size_t body_len = span.length - kPageHeaderSize;
  common::ByteReader r(body, body_len);

  try {
    const std::uint64_t entry_count = r.read_varint();
    db.string_table.reserve(entry_count < 256
                                ? static_cast<std::size_t>(entry_count)
                                : static_cast<std::size_t>(256));
    for (std::uint64_t i = 0; i < entry_count; ++i) {
      const std::uint64_t len = r.read_varint();
      r.require(static_cast<std::size_t>(len));
      db.string_table.push_back(r.read_string(static_cast<std::size_t>(len)));
    }
  } catch (const common::ParseError&) {
    // Truncated/garbled table: retain the entries we already decoded. STRING_REF
    // resolution naturally degrades to "unresolved" for missing ids.
  }
}

}  // namespace minidb
}  // namespace bw
