// bytewright - minidb record cursor / scan (impl).
//
// Cursor is a thin, std::map-iterator-backed stream over Database::records. The
// map's natural ordering is exactly (page, slot) ascending, so next() is just an
// iterator bump and seek() is lower_bound. project_row()/field_to_string() reuse
// the same value rendering as the top-level summary, kept here so a table view
// does not need to pull in minidb.cpp.
#include "minidb/cursor.hpp"

#include <cstdio>

namespace bw {
namespace minidb {

bool Cursor::next() noexcept {
  if (!started_) {
    // First call positions on the first record.
    it_ = db_->records.begin();
    started_ = true;
    return it_ != db_->records.end();
  }
  if (it_ == db_->records.end()) {
    return false;  // already exhausted; stay put
  }
  ++it_;
  return it_ != db_->records.end();
}

bool Cursor::seek(std::uint32_t page, std::uint16_t slot) noexcept {
  it_ = db_->records.lower_bound(RecordKey{page, slot});
  started_ = true;
  return it_ != db_->records.end();
}

const Field* Cursor::field(std::size_t index) const noexcept {
  if (!valid()) {
    return nullptr;
  }
  const Record& rec = it_->second;
  if (index >= rec.fields.size()) {
    return nullptr;
  }
  return &rec.fields[index];
}

std::string field_to_string(const Field& f) {
  switch (f.type) {
    case FieldType::kNull:
      return "null";
    case FieldType::kBool:
      return f.b ? "true" : "false";
    case FieldType::kInt:
      return std::to_string(f.i64);
    case FieldType::kUint:
      return std::to_string(f.u64);
    case FieldType::kFloat: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%g", f.f64);
      return std::string(buf);
    }
    case FieldType::kStringRef:
      // A resolved ref renders as its text; an unresolved one keeps the raw id so
      // the projection is never silently empty for a dangling reference.
      if (f.ref_resolved) {
        return f.str;
      }
      return "#" + std::to_string(f.ref_id);
    case FieldType::kInlineString:
      return f.str;
    case FieldType::kBlob:
      return "blob[" + std::to_string(f.blob.size()) + "]";
  }
  return "?";
}

std::vector<std::string> project_row(const Record& rec) {
  std::vector<std::string> row;
  row.reserve(rec.fields.size());
  for (const Field& f : rec.fields) {
    row.push_back(field_to_string(f));
  }
  return row;
}

}  // namespace minidb
}  // namespace bw
