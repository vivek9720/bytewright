// bytewright - binpack model types
//
// The "BPK1" container is a small tagged binary format: a fixed header, a flat
// section table, and per-section bodies that hold a stream of self-describing
// records. Records can nest (arrays, groups, key/value pairs), so the parsed
// model is a tree. These types are the decoded, in-memory representation that
// parse() produces and summarize() walks.
#ifndef BYTEWRIGHT_BINPACK_MODEL_HPP
#define BYTEWRIGHT_BINPACK_MODEL_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bw {
namespace binpack {

// Record payload tags. These are the on-wire `tag` byte of a record and also
// drive how the payload is interpreted. Keeping the enum values identical to
// the wire encoding keeps the decoder branchy-but-obvious.
enum class RecordTag : std::uint8_t {
  kInt = 0x01,     // svarint -> int64
  kUint = 0x02,    // varint  -> uint64
  kFloat = 0x03,   // 8 bytes f64 LE
  kString = 0x04,  // utf-8 bytes
  kBlob = 0x05,    // raw bytes
  kArray = 0x06,   // varint count, then `count` nested records
  kGroup = 0x07,   // nested records until the payload is exhausted
  kKeyval = 0x08,  // key_len varint + key bytes + exactly one nested record
};

const char* tag_name(RecordTag tag) noexcept;

// Section types from the section table. Only METADATA gets special collection
// treatment; the rest are parsed uniformly as record streams.
enum class SectionType : std::uint16_t {
  kFree = 0,
  kData = 1,
  kMetadata = 2,
  kIndex = 3,
  kStrings = 4,
};

const char* section_type_name(SectionType type) noexcept;

// A single decoded record node. Rather than std::variant we use an explicit
// discriminated struct: the active member is selected by `tag`. Scalar payloads
// live in the obvious field; the container tags (array/group/keyval) populate
// `children`, and keyval additionally fills `key`. This keeps the recursive
// walk in summarize() and the fuzz harness easy to read.
struct Record {
  RecordTag tag{};
  bool group = false;  // record flags bit0: nested/group marker

  std::int64_t i64 = 0;
  std::uint64_t u64 = 0;
  double f64 = 0.0;
  std::string str;                    // kString and the kKeyval key text
  std::vector<std::uint8_t> blob;     // kBlob raw bytes
  std::vector<Record> children;       // kArray / kGroup / kKeyval value
  std::string key;                    // kKeyval key
};

// One section table entry plus its decoded body.
struct Section {
  SectionType type{};
  std::uint16_t id = 0;
  std::uint16_t flags = 0;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
  std::uint32_t item_count = 0;
  std::uint32_t crc32_stored = 0;
  bool body_checksum_ok = true;       // per-section CRC over the body bytes
  std::vector<Record> records;
};

// The fully parsed container. `checksum_ok` is the AND of the header CRC, every
// section body CRC, and (when present) the trailer CRC. Individual failures are
// preserved on the relevant struct so a caller can locate the bad piece.
struct Container {
  std::uint16_t version = 0;
  std::uint16_t flags = 0;
  std::uint16_t section_count = 0;

  bool has_metadata = false;
  bool has_trailer_crc = false;

  bool header_checksum_ok = true;
  bool trailer_checksum_ok = true;
  bool checksum_ok = true;  // overall: header && all sections && trailer

  std::vector<Section> sections;

  // Convenience view collected from any METADATA section's KEYVAL records.
  std::vector<std::pair<std::string, Record>> metadata;
};

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_MODEL_HPP
