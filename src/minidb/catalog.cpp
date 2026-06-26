// bytewright - minidb catalog / summary view (impl).
//
// build_catalog() makes a single pass over db.pages (slot utilisation + type
// histogram) and one over db.string_table (string stats), then folds in the
// committed-record count. Everything is derived from the in-memory model, so the
// function is total and side-effect free. format_catalog() renders the roll-up.
#include "minidb/catalog.hpp"

namespace bw {
namespace minidb {

namespace {

// The histogram spans every defined PageType (kFree..kOverflow). Sizing the
// vector to one past the largest enumerator keeps count_of() indexable without a
// map. Update this if PageType grows.
constexpr std::size_t kPageTypeSlots =
    static_cast<std::size_t>(PageType::kOverflow) + 1;

}  // namespace

std::size_t Catalog::count_of(PageType t) const noexcept {
  const std::size_t idx = static_cast<std::size_t>(t);
  if (idx >= page_type_counts.size()) {
    return 0;
  }
  return page_type_counts[idx];
}

Catalog build_catalog(const Database& db) {
  Catalog cat;
  cat.page_type_counts.assign(kPageTypeSlots, 0);
  cat.total_pages = db.pages.size();
  cat.total_live_records = db.records.size();

  // Per-page pass: histogram + slot utilisation.
  cat.pages.reserve(db.pages.size());
  for (const Page& p : db.pages) {
    const std::size_t type_idx = static_cast<std::size_t>(p.type);
    if (type_idx < cat.page_type_counts.size()) {
      ++cat.page_type_counts[type_idx];
    }
    // (A page whose type byte is outside the known range is simply not counted in
    // the histogram; it still appears in the per-page table below.)

    PageStats ps;
    ps.index = p.index;
    ps.type = p.type;
    ps.total_slots = p.slots.size();
    ps.truncated = p.truncated;
    for (const Slot& s : p.slots) {
      if (s.in_bounds && s.rec_length > 0) {
        ++ps.used_slots;
      } else {
        ++ps.dead_slots;
      }
    }
    cat.total_used_slots += ps.used_slots;
    cat.total_dead_slots += ps.dead_slots;
    cat.pages.push_back(ps);
  }

  // String-table pass.
  cat.strings.count = db.string_table.size();
  for (const std::string& s : db.string_table) {
    cat.strings.total_bytes += s.size();
    if (s.size() > cat.strings.max_bytes) {
      cat.strings.max_bytes = s.size();
    }
    if (s.empty()) {
      ++cat.strings.empty_count;
    }
  }

  return cat;
}

std::string format_catalog(const Catalog& cat) {
  std::string out;
  out += "minidb catalog\n";
  out += "  pages          : " + std::to_string(cat.total_pages) + "\n";

  // Histogram line, one token per known page type with a non-zero count.
  out += "  page types     :";
  bool any = false;
  for (std::size_t t = 0; t < cat.page_type_counts.size(); ++t) {
    const std::size_t n = cat.page_type_counts[t];
    if (n == 0) {
      continue;
    }
    out += " ";
    out += page_type_name(static_cast<PageType>(t));
    out += "=" + std::to_string(n);
    any = true;
  }
  if (!any) {
    out += " (none)";
  }
  out += "\n";

  out += "  live records   : " + std::to_string(cat.total_live_records) + "\n";
  out += "  slots used/dead: " + std::to_string(cat.total_used_slots) + "/" +
         std::to_string(cat.total_dead_slots) + "\n";
  out += "  strings        : " + std::to_string(cat.strings.count) +
         " entries, " + std::to_string(cat.strings.total_bytes) +
         " bytes, max " + std::to_string(cat.strings.max_bytes) + ", " +
         std::to_string(cat.strings.empty_count) + " empty\n";

  // Per-page utilisation table.
  out += "  per-page:\n";
  for (const PageStats& ps : cat.pages) {
    out += "    page " + std::to_string(ps.index) + ": " +
           page_type_name(ps.type) + ", slots " +
           std::to_string(ps.used_slots) + "/" +
           std::to_string(ps.total_slots) + " used";
    if (ps.dead_slots != 0) {
      out += ", " + std::to_string(ps.dead_slots) + " dead";
    }
    if (ps.truncated) {
      out += ", truncated";
    }
    out += "\n";
  }

  return out;
}

}  // namespace minidb
}  // namespace bw
