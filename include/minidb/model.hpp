// bytewright - minidb model types
//
// minidb decodes the "MDB1" embedded record store: a fixed 32-byte file header,
// a flat array of fixed-size pages (each with a page header + slot directory),
// an optional string table page, and a trailing journal/WAL of transactional
// operations. open() parses the static structure and then *replays* the journal
// to produce the final committed record set. These types are the decoded,
// in-memory representation that open() produces and summarize() walks.
#ifndef BYTEWRIGHT_MINIDB_MODEL_HPP
#define BYTEWRIGHT_MINIDB_MODEL_HPP

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bw {
namespace minidb {

// On-wire page type (page header byte 0). FREE pages carry no slots; DATA pages
// hold records; STRINGS holds the string table; INDEX/OVERFLOW are recognised
// and walked structurally but their bodies are not specially interpreted.
enum class PageType : std::uint8_t {
  kFree = 0,
  kData = 1,
  kStrings = 2,
  kIndex = 3,
  kOverflow = 4,
};

const char* page_type_name(PageType t) noexcept;

// On-wire field type tag (record field byte 0). Drives how the value bytes that
// follow are interpreted. Unknown tags are a hard error (kBadType).
enum class FieldType : std::uint8_t {
  kNull = 0,          // no payload
  kBool = 1,          // u8 (0/1)
  kInt = 2,           // svarint -> int64
  kUint = 3,          // varint  -> uint64
  kFloat = 4,         // 8 bytes f64 LE
  kStringRef = 5,     // varint id -> resolve against the string table
  kInlineString = 6,  // varint len + bytes
  kBlob = 7,          // varint len + bytes
};

const char* field_type_name(FieldType t) noexcept;

// On-wire journal op (journal entry byte 0).
enum class JournalOp : std::uint8_t {
  kBegin = 1,
  kInsert = 2,   // page u32, slot u16, rec_len varint, rec_bytes
  kUpdate = 3,   // same shape as INSERT
  kDelete = 4,   // page u32, slot u16
  kCommit = 5,
  kRollback = 6,
};

const char* journal_op_name(JournalOp op) noexcept;

// A single decoded record field. We use an explicit discriminated struct rather
// than std::variant: the active member is selected by `type`. STRING_REF carries
// both the raw id (`ref_id`) and, once resolved against the string table, the
// resolved text in `str`.
struct Field {
  FieldType type = FieldType::kNull;

  bool b = false;
  std::int64_t i64 = 0;
  std::uint64_t u64 = 0;
  double f64 = 0.0;
  std::string str;                 // kInlineString text, or resolved STRING_REF
  std::uint64_t ref_id = 0;        // kStringRef raw id (pre-resolution)
  bool ref_resolved = false;       // kStringRef: did the id map to a table entry
  std::vector<std::uint8_t> blob;  // kBlob raw bytes
};

// A decoded record: an ordered list of typed fields.
struct Record {
  std::vector<Field> fields;
};

// One slot directory entry: where a record lives inside its page.
struct Slot {
  std::uint16_t rec_offset = 0;  // relative to page start
  std::uint16_t rec_length = 0;
  bool in_bounds = true;         // false if the slot pointed outside the page
};

// A decoded page: its header plus slot metadata. The actual record bytes are not
// duplicated here; committed records are surfaced via Database::records after the
// journal has been replayed on top of the static page contents.
struct Page {
  std::uint32_t index = 0;        // page number (0-based)
  PageType type = PageType::kFree;
  std::uint8_t flags = 0;
  std::uint16_t slot_count = 0;
  std::uint16_t free_start = 0;
  std::uint16_t overflow_next = 0xFFFF;  // 0xFFFF = none
  bool truncated = false;         // page ran past EOF and was clamped/skipped
  std::vector<Slot> slots;
};

// Key into the logical record store: (page, slot).
using RecordKey = std::pair<std::uint32_t, std::uint16_t>;

// The fully parsed + replayed database. `checksum_ok` reflects the header CRC
// (lenient: a mismatch is recorded, not thrown, in the default mode).
struct Database {
  std::uint16_t version = 0;
  std::uint16_t page_size = 0;
  std::uint32_t page_count = 0;
  std::uint32_t root_page = 0;
  std::uint32_t string_table_page = 0xFFFFFFFF;
  std::uint32_t journal_offset = 0;
  std::uint32_t flags = 0;

  bool checksum_ok = true;        // header CRC matched
  bool has_journal = false;

  std::vector<std::string> string_table;
  std::vector<Page> pages;

  // Journal replay bookkeeping, surfaced for the summary/diagnostics.
  std::uint32_t committed_ops = 0;
  std::uint32_t rolled_back_ops = 0;

  // The post-replay committed record store: (page,slot) -> decoded record.
  std::map<RecordKey, Record> records;
};

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_MODEL_HPP
