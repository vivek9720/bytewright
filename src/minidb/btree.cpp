// bytewright - minidb INDEX page decoding + ordered lookup (impl).
//
// See btree.hpp for the on-wire layout. The decode walks the page body through a
// bounded ByteReader, tolerating a truncated tail, then sorts the decoded entries
// by raw key bytes so point lookup and range scan can binary-search them
// regardless of the writer's ordering. All comparisons are unsigned byte-string
// (std::string already compares chars; we route through an explicit unsigned
// comparator so high bytes order after low bytes on platforms with signed char).
#include "minidb/btree.hpp"

#include <algorithm>

#include "common/byte_reader.hpp"
#include "common/status.hpp"
#include "minidb/format.hpp"

namespace bw {
namespace minidb {

namespace {

// Unsigned lexicographic comparison of two byte strings. std::string::operator<
// can be signed-char on some toolchains; we compare the raw bytes so a 0x80 byte
// reliably orders after 0x7F. Returns <0, 0, >0 like memcmp.
int key_compare(const std::string& a, const std::string& b) noexcept {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca != cb) {
      return ca < cb ? -1 : 1;
    }
  }
  if (a.size() == b.size()) {
    return 0;
  }
  return a.size() < b.size() ? -1 : 1;
}

bool key_less(const std::string& a, const std::string& b) noexcept {
  return key_compare(a, b) < 0;
}

}  // namespace

const IndexEntry* IndexPage::lookup(const std::string& key) const noexcept {
  // Binary search the sorted entries for the first one not ordered before key,
  // then confirm an exact match. entries is sorted ascending by key_less.
  std::size_t lo = 0;
  std::size_t hi = entries.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (key_compare(entries[mid].key, key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < entries.size() && key_compare(entries[lo].key, key) == 0) {
    return &entries[lo];
  }
  return nullptr;
}

std::vector<IndexEntry> IndexPage::range(const std::string& lo,
                                         const std::string& hi) const {
  std::vector<IndexEntry> out;
  // An inverted range (lo > hi) selects nothing rather than throwing.
  if (key_compare(lo, hi) > 0) {
    return out;
  }
  for (const IndexEntry& e : entries) {
    if (key_compare(e.key, lo) < 0) {
      continue;  // before the window
    }
    if (key_compare(e.key, hi) > 0) {
      break;  // past the window; entries are sorted so we can stop
    }
    out.push_back(e);
  }
  return out;
}

IndexPage decode_index_page(const std::uint8_t* page_bytes,
                            std::size_t page_len, std::uint32_t page_index) {
  IndexPage out;
  out.page_index = page_index;

  // Without a full page header we cannot identify the type; treat as empty.
  if (page_len < kPageHeaderSize) {
    out.truncated = true;
    return out;
  }
  // Byte 0 of the page header is the page type; only INDEX pages carry an index
  // body. Anything else yields an empty (non-truncated) result.
  if (static_cast<PageType>(page_bytes[0]) != PageType::kIndex) {
    return out;
  }

  // Body reader: scoped to the page bytes, starting just past the 8-byte header.
  const std::uint8_t* body = page_bytes + kPageHeaderSize;
  const std::size_t body_len = page_len - kPageHeaderSize;
  common::ByteReader r(body, body_len);

  try {
    const std::uint64_t entry_count = r.read_varint();
    out.entries.reserve(entry_count < 256
                            ? static_cast<std::size_t>(entry_count)
                            : static_cast<std::size_t>(256));
    for (std::uint64_t i = 0; i < entry_count; ++i) {
      IndexEntry e;
      const std::uint64_t key_len = r.read_varint();
      r.require(static_cast<std::size_t>(key_len));
      e.key = r.read_string(static_cast<std::size_t>(key_len));
      e.page = r.read_u32le();
      e.slot = r.read_u16le();
      out.entries.push_back(std::move(e));
    }
  } catch (const common::ParseError&) {
    // Truncated/garbled body: retain the cleanly decoded prefix and flag it. The
    // surviving entries are still a usable (partial) index.
    out.truncated = true;
  }

  // Sort by raw key so lookup()/range() can rely on ordered entries even if the
  // writer emitted them unsorted. std::stable_sort keeps duplicate keys in their
  // original (insertion) order, so lookup() returning "the first match" is
  // deterministic.
  std::stable_sort(out.entries.begin(), out.entries.end(),
                   [](const IndexEntry& a, const IndexEntry& b) {
                     return key_less(a.key, b.key);
                   });

  return out;
}

}  // namespace minidb
}  // namespace bw
