// bytewright - minidb record cursor / scan.
//
// open() leaves the committed record store in Database::records, a std::map keyed
// by (page, slot) and therefore already iterable in (page, slot) order. A Cursor
// wraps that ordering behind a small, stateful iteration API - next()/seek()/the
// current key and a field(index) accessor - so callers can stream over the
// committed records without re-deriving the map iterator dance. A separate helper
// projects a record into a row of display strings (resolving STRING_REFs through
// the already-decoded fields), which is what a textual table view wants.
//
// The Cursor borrows the Database; it must not outlive it.
#ifndef BYTEWRIGHT_MINIDB_CURSOR_HPP
#define BYTEWRIGHT_MINIDB_CURSOR_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "minidb/model.hpp"

namespace bw {
namespace minidb {

class Cursor {
 public:
  // Construct positioned before the first record: the first next() lands on the
  // lowest (page, slot) key. (A freshly constructed cursor is not yet valid().)
  explicit Cursor(const Database& db) noexcept
      : db_(&db), it_(db.records.end()), started_(false) {}

  // Advance to the next record in (page, slot) order. Returns true and makes the
  // cursor valid() if a record is available, false once exhausted. The first call
  // positions on the first record.
  bool next() noexcept;

  // True when the cursor points at a live record (after a successful next()/seek
  // and before exhaustion).
  bool valid() const noexcept {
    return started_ && it_ != db_->records.end();
  }

  // Position on the first record whose key is >= (page, slot). Returns true and
  // makes the cursor valid() if such a record exists, false (and invalid) if the
  // seek lands past the last record. Unlike next(), seek() positions *on* the
  // found record, so valid()/key()/record() are usable immediately.
  bool seek(std::uint32_t page, std::uint16_t slot) noexcept;

  // The current record's locator. Precondition: valid().
  RecordKey key() const noexcept { return it_->first; }
  std::uint32_t page() const noexcept { return it_->first.first; }
  std::uint16_t slot() const noexcept { return it_->first.second; }

  // The current record. Precondition: valid().
  const Record& record() const noexcept { return it_->second; }

  // The current record's field at `index`, or nullptr if `index` is out of range
  // (or the cursor is not valid()). Borrows from the record.
  const Field* field(std::size_t index) const noexcept;

 private:
  const Database* db_;
  std::map<RecordKey, Record>::const_iterator it_;
  bool started_;  // has next()/seek() been called at least once
};

// Render a single field's value to a compact display string. Mirrors the format
// used by summarize() but is reusable for row projection: NULL -> "null",
// resolved STRING_REF -> its text, BLOB -> "blob[n]", etc.
std::string field_to_string(const Field& f);

// Project a record into a row of per-field display strings (column order matches
// field order). A convenience for rendering committed records as a table.
std::vector<std::string> project_row(const Record& rec);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_CURSOR_HPP
