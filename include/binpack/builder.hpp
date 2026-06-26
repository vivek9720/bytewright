// bytewright - binpack container builder
//
// The encoding counterpart to parse(). A Builder assembles a Container's logical
// content programmatically -- sections, and within each section a stream of
// (possibly nested) records -- and then serializes it to a byte buffer whose
// layout is exactly what src/binpack/parser.cpp expects to read back. That means
// the header CRC, every per-section body CRC, and (optionally) the whole-file
// trailer CRC are computed and placed identically, so build() output round-trips
// through parse() with checksum_ok == true.
//
// Records are described with a small value type, RecordSpec, rather than the
// decoded model::Record so the builder stays decoupled from the parser's output
// representation and so the helper constructors read naturally at call sites.
//
// No file IO, no globals: build() returns the bytes in a std::vector and the
// caller decides what to do with them.
#ifndef BYTEWRIGHT_BINPACK_BUILDER_HPP
#define BYTEWRIGHT_BINPACK_BUILDER_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "binpack/model.hpp"

namespace bw {
namespace binpack {

// A record to be encoded. The active payload is selected by `tag`, mirroring the
// discriminated layout of model::Record but expressed as plain inputs. The
// container tags (kArray/kGroup/kKeyval) use `children`; kKeyval additionally
// uses `key` and requires exactly one child (its value).
//
// Construction goes through the static factory helpers below; they keep call
// sites terse and make the invariants (e.g. "a keyval has one child") obvious.
struct RecordSpec {
  RecordTag tag{};
  bool group = false;  // serialized into record flags bit0

  std::int64_t i64 = 0;
  std::uint64_t u64 = 0;
  double f64 = 0.0;
  std::string str;                  // kString text / unused otherwise
  std::vector<std::uint8_t> blob;   // kBlob bytes
  std::string key;                  // kKeyval key
  std::vector<RecordSpec> children; // kArray / kGroup / kKeyval

  static RecordSpec Int(std::int64_t v) {
    RecordSpec r;
    r.tag = RecordTag::kInt;
    r.i64 = v;
    return r;
  }
  static RecordSpec Uint(std::uint64_t v) {
    RecordSpec r;
    r.tag = RecordTag::kUint;
    r.u64 = v;
    return r;
  }
  static RecordSpec Float(double v) {
    RecordSpec r;
    r.tag = RecordTag::kFloat;
    r.f64 = v;
    return r;
  }
  static RecordSpec String(std::string v) {
    RecordSpec r;
    r.tag = RecordTag::kString;
    r.str = std::move(v);
    return r;
  }
  static RecordSpec Blob(std::vector<std::uint8_t> v) {
    RecordSpec r;
    r.tag = RecordTag::kBlob;
    r.blob = std::move(v);
    return r;
  }
  static RecordSpec Array(std::vector<RecordSpec> elems) {
    RecordSpec r;
    r.tag = RecordTag::kArray;
    r.children = std::move(elems);
    return r;
  }
  // A GROUP is serialized with the group flag bit set, matching how a producer
  // marks a nested container in the wire flags byte (and how parse() reports it).
  static RecordSpec Group(std::vector<RecordSpec> recs) {
    RecordSpec r;
    r.tag = RecordTag::kGroup;
    r.group = true;
    r.children = std::move(recs);
    return r;
  }
  static RecordSpec Keyval(std::string key, RecordSpec value) {
    RecordSpec r;
    r.tag = RecordTag::kKeyval;
    r.key = std::move(key);
    r.children.push_back(std::move(value));
    return r;
  }
};

// One section under construction: its table metadata plus the records to encode
// into its body. `item_count` is written verbatim into the section table; it is
// the producer's declared count and is what validate() can cross-check against
// the actual number of top-level records.
struct SectionSpec {
  SectionType type = SectionType::kData;
  std::uint16_t id = 0;
  std::uint16_t flags = 0;
  std::uint32_t item_count = 0;
  std::vector<RecordSpec> records;
};

// Assembles sections and serializes a whole BPK1 container.
class Builder {
 public:
  Builder() = default;

  // Header flags. has_metadata (bit0) is advisory; has_trailer_crc (bit1)
  // additionally causes build() to append a whole-file trailer CRC.
  Builder& set_version(std::uint16_t v) {
    version_ = v;
    return *this;
  }
  Builder& set_has_metadata(bool on) {
    has_metadata_ = on;
    return *this;
  }
  Builder& set_has_trailer_crc(bool on) {
    has_trailer_crc_ = on;
    return *this;
  }

  // Begin a new section and return a reference to it for record population. The
  // reference is valid until the next add_section() call (it points into an
  // internal vector that may reallocate), so finish populating a section before
  // starting the next one.
  SectionSpec& add_section(SectionType type, std::uint16_t id = 0,
                           std::uint16_t flags = 0);

  // Convenience: append a record to the most recently added section.
  Builder& add_record(RecordSpec rec);

  // Convenience: declare item_count on the most recently added section. If never
  // called for a section, build() defaults that section's item_count to the
  // number of top-level records it holds.
  Builder& set_item_count(std::uint32_t n);

  std::size_t section_count() const noexcept { return sections_.size(); }

  // Serialize everything to a fresh byte buffer in the BPK1 wire layout.
  std::vector<std::uint8_t> build() const;

 private:
  std::uint16_t version_ = 1;
  bool has_metadata_ = false;
  bool has_trailer_crc_ = false;
  std::vector<SectionSpec> sections_;
  // Tracks which sections had an explicit set_item_count() so build() knows when
  // to auto-fill from the record count. Parallel to `sections_`.
  std::vector<bool> item_count_explicit_;
};

// Free helper: encode the payload bytes of a single record (everything after the
// record's tag/flags/length header). Exposed because the test suite and other
// tooling occasionally need just a payload; build() uses it internally.
std::vector<std::uint8_t> encode_record_payload(const RecordSpec& rec);

// Free helper: encode a full record (header + payload) into a byte vector.
std::vector<std::uint8_t> encode_record(const RecordSpec& rec);

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_BUILDER_HPP
