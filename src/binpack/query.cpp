// bytewright - binpack model query helpers
//
// Read-only lookups over a parsed Container. See query.hpp for contracts. All
// returned pointers reference into the supplied Container/Section and never own
// anything; nothing throws.
#include "binpack/query.hpp"

namespace bw {
namespace binpack {

namespace {

// Append pointers to every record carrying `tag` found in `recs` (and, when
// recursive, their children) to `out`, in traversal order.
void collect_into(const std::vector<Record>& recs, RecordTag tag,
                  bool recursive, std::vector<const Record*>& out) {
  for (const Record& rec : recs) {
    if (rec.tag == tag) {
      out.push_back(&rec);
    }
    if (recursive && !rec.children.empty()) {
      collect_into(rec.children, tag, recursive, out);
    }
  }
}

}  // namespace

const Section* find_section(const Container& c, SectionType type) noexcept {
  for (const Section& s : c.sections) {
    if (s.type == type) return &s;
  }
  return nullptr;
}

Section* find_section(Container& c, SectionType type) noexcept {
  for (Section& s : c.sections) {
    if (s.type == type) return &s;
  }
  return nullptr;
}

const Section* find_section_by_id(const Container& c,
                                  std::uint16_t id) noexcept {
  for (const Section& s : c.sections) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

Section* find_section_by_id(Container& c, std::uint16_t id) noexcept {
  for (Section& s : c.sections) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::vector<const Section*> find_sections(const Container& c,
                                          SectionType type) {
  std::vector<const Section*> out;
  for (const Section& s : c.sections) {
    if (s.type == type) out.push_back(&s);
  }
  return out;
}

const Record* metadata_value(const Container& c,
                             const std::string& key) noexcept {
  for (const auto& kv : c.metadata) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::optional<std::string> metadata_string(const Container& c,
                                           const std::string& key) {
  const Record* v = metadata_value(c, key);
  if (v != nullptr && v->tag == RecordTag::kString) {
    return v->str;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> metadata_uint(const Container& c,
                                           const std::string& key) {
  const Record* v = metadata_value(c, key);
  if (v != nullptr && v->tag == RecordTag::kUint) {
    return v->u64;
  }
  return std::nullopt;
}

std::optional<std::int64_t> metadata_int(const Container& c,
                                         const std::string& key) {
  const Record* v = metadata_value(c, key);
  if (v != nullptr && v->tag == RecordTag::kInt) {
    return v->i64;
  }
  return std::nullopt;
}

std::vector<const Record*> collect_records(const Container& c, RecordTag tag,
                                           bool recursive) {
  std::vector<const Record*> out;
  for (const Section& s : c.sections) {
    collect_into(s.records, tag, recursive, out);
  }
  return out;
}

std::vector<const Record*> collect_records(const Section& section,
                                           RecordTag tag, bool recursive) {
  std::vector<const Record*> out;
  collect_into(section.records, tag, recursive, out);
  return out;
}

}  // namespace binpack
}  // namespace bw
