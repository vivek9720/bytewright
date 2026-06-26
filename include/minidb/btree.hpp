// bytewright - minidb INDEX page decoding + ordered lookup.
//
// The core decoder (src/minidb/*.cpp) recognises INDEX pages structurally but
// never interprets their bodies. This module defines and decodes a concrete,
// leaf-only index layout that maps a byte key to a record locator (page, slot),
// and offers ordered point lookup plus range scans over the decoded entries.
//
// INDEX page body layout (immediately after the 8-byte page header):
//   entry_count   varint
//   entries[entry_count], each:
//     key_len     varint
//     key         key_len bytes
//     page        u32   (the record's page index)
//     slot        u16   (the record's slot within that page)
//
// Keys are compared as raw byte strings (lexicographic, unsigned). A well-formed
// index stores its entries already sorted by key; we do not trust that and sort
// the decoded entries ourselves so lookup() can binary-search regardless. A
// truncated or garbled body is tolerated: we keep the entries that decoded
// cleanly and stop at the first short read (mirroring the string-table decoder).
#ifndef BYTEWRIGHT_MINIDB_BTREE_HPP
#define BYTEWRIGHT_MINIDB_BTREE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "minidb/model.hpp"

namespace bw {
namespace minidb {

// One decoded index entry: a key and the record locator it points at.
struct IndexEntry {
  std::string key;                 // raw key bytes (may contain NULs)
  std::uint32_t page = 0;          // target record's page index
  std::uint16_t slot = 0;          // target record's slot within that page
};

// A decoded INDEX page: its source page index plus the ordered entries. `entries`
// is sorted by key ascending; `truncated` is set if the body ran short and the
// tail of the declared entries could not be read.
struct IndexPage {
  std::uint32_t page_index = 0;
  bool truncated = false;
  std::vector<IndexEntry> entries;

  // Ordered point lookup. Returns a pointer to the first entry whose key equals
  // `key`, or nullptr if no entry matches. The pointer is valid until `entries`
  // is mutated (it borrows from the vector).
  const IndexEntry* lookup(const std::string& key) const noexcept;

  // Inclusive range scan: every entry with lo <= key <= hi, in key order.
  std::vector<IndexEntry> range(const std::string& lo,
                                const std::string& hi) const;
};

// Decode an INDEX page from a single contiguous page image (8-byte page header
// followed by the index body described above). `page_bytes`/`page_len` cover the
// whole page (header included); `page_index` is recorded on the result for
// diagnostics. The body is read through a bounded ByteReader so a malformed
// count/length yields a structured short read, after which decoding stops with
// `truncated = true`. If the page header's type byte is not INDEX, the result is
// empty (no entries).
IndexPage decode_index_page(const std::uint8_t* page_bytes,
                            std::size_t page_len, std::uint32_t page_index);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_BTREE_HPP
