// bytewright - minidb catalog / summary view.
//
// A compact, structured digest of a decoded Database, computed by walking the
// already-parsed model (no buffer re-reading). Where summarize() renders a long
// human dump, the Catalog is a machine-friendly roll-up: a page-type histogram,
// the live committed-record count, string-table statistics, and per-page slot
// utilisation. It is a pure function of the Database - building it never mutates
// the model and never touches the original bytes.
#ifndef BYTEWRIGHT_MINIDB_CATALOG_HPP
#define BYTEWRIGHT_MINIDB_CATALOG_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "minidb/model.hpp"

namespace bw {
namespace minidb {

// Per-page slot utilisation. `used_slots` counts in-bounds, non-empty slots;
// `total_slots` is the directory length actually decoded (page.slots.size()).
struct PageStats {
  std::uint32_t index = 0;
  PageType type = PageType::kFree;
  std::size_t total_slots = 0;
  std::size_t used_slots = 0;     // in_bounds && rec_length > 0
  std::size_t dead_slots = 0;     // out-of-bounds or zero-length entries
  bool truncated = false;
};

// String-table statistics.
struct StringStats {
  std::size_t count = 0;        // number of entries
  std::size_t total_bytes = 0;  // sum of entry lengths
  std::size_t max_bytes = 0;    // longest single entry
  std::size_t empty_count = 0;  // entries that are the empty string
};

// The full catalog roll-up.
struct Catalog {
  // Page-type histogram, indexed by the PageType enum value (0..4). counts[t] is
  // the number of pages of type t. Sized to cover every defined PageType.
  std::vector<std::size_t> page_type_counts;

  std::size_t total_pages = 0;
  std::size_t total_live_records = 0;  // db.records.size()
  std::size_t total_used_slots = 0;    // summed across pages
  std::size_t total_dead_slots = 0;

  StringStats strings;
  std::vector<PageStats> pages;  // parallel to db.pages

  // Convenience: count of pages of a given type (bounds-checked; 0 if the type
  // is somehow outside the histogram).
  std::size_t count_of(PageType t) const noexcept;
};

// Build the catalog from a decoded Database.
Catalog build_catalog(const Database& db);

// Render the catalog as a short, multi-line human summary (one line per section
// plus a per-page utilisation table). Distinct from summarize(), which dumps the
// full record contents.
std::string format_catalog(const Catalog& cat);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_CATALOG_HPP
