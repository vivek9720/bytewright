// bytewright - minidb unit tests.
//
// Inputs are assembled with the shared ByteWriter so the wire format is spelled
// out in one place beside the assertions. A small set of helpers builds valid
// pages/records/journals; individual tests then perturb one thing each.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "bw_test.hpp"
#include "common/byte_writer.hpp"
#include "common/checksum.hpp"
#include "common/status.hpp"
#include "minidb/format.hpp"
#include "minidb/minidb.hpp"
#include "minidb/model.hpp"

namespace {

using bw::common::ByteWriter;
using namespace bw::minidb;

// ---- builders -------------------------------------------------------------

// Encode a 32-byte file header, computing and patching the header CRC over the
// first 28 bytes. If override_crc is non-null, that value is stored instead (so
// a test can force a checksum mismatch).
std::vector<std::uint8_t> make_header(std::uint16_t page_size,
                                      std::uint32_t page_count,
                                      std::uint32_t root_page,
                                      std::uint32_t string_table_page,
                                      std::uint32_t journal_offset,
                                      std::uint32_t flags,
                                      const std::uint32_t* override_crc) {
  ByteWriter w;
  w.put_u8('M');
  w.put_u8('D');
  w.put_u8('B');
  w.put_u8('1');
  w.put_u16le(1);  // version
  w.put_u16le(page_size);
  w.put_u32le(page_count);
  w.put_u32le(root_page);
  w.put_u32le(string_table_page);
  w.put_u32le(journal_offset);
  w.put_u32le(flags);
  std::uint32_t crc = override_crc
                          ? *override_crc
                          : bw::common::crc32(w.bytes().data(), 28);
  w.put_u32le(crc);
  return w.bytes();
}

// A record body: field_count varint then the raw field bytes already built.
std::vector<std::uint8_t> make_record(std::uint64_t field_count,
                                      const std::vector<std::uint8_t>& fields) {
  ByteWriter w;
  w.put_varint(field_count);
  w.put_bytes(fields);
  return w.bytes();
}

// Build a full page image of exactly `page_size` bytes: 8-byte header, slot
// directory, then record bytes placed at their declared offsets. `records` maps
// a slot index to its raw record bytes; offsets are laid out right after the
// directory.
std::vector<std::uint8_t> make_page(
    PageType type, std::uint16_t page_size,
    std::uint16_t overflow_next,
    const std::vector<std::vector<std::uint8_t>>& records) {
  const std::uint16_t slot_count = static_cast<std::uint16_t>(records.size());
  std::vector<std::uint8_t> page(page_size, 0);

  // Lay records out sequentially after the directory.
  std::size_t cursor = kPageHeaderSize +
                       static_cast<std::size_t>(slot_count) * kSlotEntrySize;

  // Page header.
  page[0] = static_cast<std::uint8_t>(type);
  page[1] = 0;  // flags
  page[2] = static_cast<std::uint8_t>(slot_count & 0xFF);
  page[3] = static_cast<std::uint8_t>((slot_count >> 8) & 0xFF);
  // free_start filled in after layout.
  page[6] = static_cast<std::uint8_t>(overflow_next & 0xFF);
  page[7] = static_cast<std::uint8_t>((overflow_next >> 8) & 0xFF);

  for (std::size_t s = 0; s < records.size(); ++s) {
    const auto& rec = records[s];
    const std::uint16_t off = static_cast<std::uint16_t>(cursor);
    const std::uint16_t len = static_cast<std::uint16_t>(rec.size());
    const std::size_t dir = kPageHeaderSize + s * kSlotEntrySize;
    page[dir + 0] = static_cast<std::uint8_t>(off & 0xFF);
    page[dir + 1] = static_cast<std::uint8_t>((off >> 8) & 0xFF);
    page[dir + 2] = static_cast<std::uint8_t>(len & 0xFF);
    page[dir + 3] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
    for (std::size_t b = 0; b < rec.size(); ++b) {
      page[cursor + b] = rec[b];
    }
    cursor += rec.size();
  }

  const std::uint16_t free_start = static_cast<std::uint16_t>(cursor);
  page[4] = static_cast<std::uint8_t>(free_start & 0xFF);
  page[5] = static_cast<std::uint8_t>((free_start >> 8) & 0xFF);
  return page;
}

// A STRINGS page body: entry_count varint then (varint len + bytes) per entry.
std::vector<std::uint8_t> make_strings_record(
    const std::vector<std::string>& entries) {
  ByteWriter w;
  w.put_varint(entries.size());
  for (const auto& e : entries) {
    w.put_varint(e.size());
    w.put_string(e);
  }
  return w.bytes();
}

// Append a journal INSERT/UPDATE entry.
void put_journal_set(ByteWriter& w, JournalOp op, std::uint32_t page,
                     std::uint16_t slot,
                     const std::vector<std::uint8_t>& rec) {
  w.put_u8(static_cast<std::uint8_t>(op));
  w.put_u32le(page);
  w.put_u16le(slot);
  w.put_varint(rec.size());
  w.put_bytes(rec);
}

void put_journal_delete(ByteWriter& w, std::uint32_t page, std::uint16_t slot) {
  w.put_u8(static_cast<std::uint8_t>(JournalOp::kDelete));
  w.put_u32le(page);
  w.put_u16le(slot);
}

void put_journal_op(ByteWriter& w, JournalOp op) {
  w.put_u8(static_cast<std::uint8_t>(op));
}

// Append `bytes` to `into`.
void append(std::vector<std::uint8_t>& into,
            const std::vector<std::uint8_t>& bytes) {
  into.insert(into.end(), bytes.begin(), bytes.end());
}

}  // namespace

// ---- tests ----------------------------------------------------------------

// A valid DB with a STRINGS page (page 0) and a DATA page (page 1) whose journal
// inserts a record using STRING_REF / INT / FLOAT / BLOB. We drive the record in
// via the journal so the committed-record decode path resolves the STRING_REF.
BW_TEST(valid_db_with_stringref_int_float_blob) {
  const std::uint16_t page_size = 128;

  // page 0: STRINGS with two entries.
  auto strings = make_strings_record({"hello", "world"});
  auto page0 = make_page(PageType::kStrings, page_size, 0xFFFF, {strings});

  // page 1: empty DATA page (the record arrives via the journal).
  auto page1 = make_page(PageType::kData, page_size, 0xFFFF, {});

  // Record: STRING_REF(1) + INT(-7) + FLOAT(2.5) + BLOB([0xDE,0xAD]).
  ByteWriter fields;
  fields.put_u8(static_cast<std::uint8_t>(FieldType::kStringRef));
  fields.put_varint(1);
  fields.put_u8(static_cast<std::uint8_t>(FieldType::kInt));
  fields.put_svarint(-7);
  fields.put_u8(static_cast<std::uint8_t>(FieldType::kFloat));
  fields.put_u64le(0x4004000000000000ULL);  // 2.5 as IEEE-754 f64 LE bits
  fields.put_u8(static_cast<std::uint8_t>(FieldType::kBlob));
  fields.put_varint(2);
  fields.put_u8(0xDE);
  fields.put_u8(0xAD);
  auto rec = make_record(4, fields.bytes());

  // Journal: BEGIN, INSERT(page 1, slot 0, rec), COMMIT.
  ByteWriter jw;
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_set(jw, JournalOp::kInsert, 1, 0, rec);
  put_journal_op(jw, JournalOp::kCommit);

  // Assemble: header + page0 + page1 + journal. journal_offset is the absolute
  // offset where the journal begins (right after the two pages).
  const std::uint32_t journal_offset =
      static_cast<std::uint32_t>(kHeaderSize + page0.size() + page1.size());
  auto hdr = make_header(page_size, /*page_count=*/2, /*root=*/1,
                         /*string_table_page=*/0, journal_offset,
                         /*flags=*/0, nullptr);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, page1);
  append(buf, jw.bytes());

  Database db = open(buf.data(), buf.size());
  BW_CHECK(db.checksum_ok);
  BW_CHECK_EQ(db.string_table.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(db.string_table[1], std::string("world"));
  BW_CHECK_EQ(db.records.size(), static_cast<std::size_t>(1));

  const Record& got = db.records.at(RecordKey{1, 0});
  BW_CHECK_EQ(got.fields.size(), static_cast<std::size_t>(4));
  BW_CHECK(got.fields[0].type == FieldType::kStringRef);
  BW_CHECK(got.fields[0].ref_resolved);
  BW_CHECK_EQ(got.fields[0].str, std::string("world"));
  BW_CHECK(got.fields[1].type == FieldType::kInt);
  BW_CHECK_EQ(got.fields[1].i64, static_cast<std::int64_t>(-7));
  BW_CHECK(got.fields[2].type == FieldType::kFloat);
  BW_CHECK(got.fields[2].f64 == 2.5);
  BW_CHECK(got.fields[3].type == FieldType::kBlob);
  BW_CHECK_EQ(got.fields[3].blob.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(got.fields[3].blob[0], static_cast<std::uint8_t>(0xDE));
}

// BEGIN -> INSERT -> COMMIT adds a record; BEGIN -> INSERT -> ROLLBACK does not.
BW_TEST(journal_commit_adds_rollback_discards) {
  const std::uint16_t page_size = 64;
  auto page0 = make_page(PageType::kData, page_size, 0xFFFF, {});

  // Two simple single-field UINT records.
  ByteWriter f1;
  f1.put_u8(static_cast<std::uint8_t>(FieldType::kUint));
  f1.put_varint(111);
  auto rec1 = make_record(1, f1.bytes());

  ByteWriter f2;
  f2.put_u8(static_cast<std::uint8_t>(FieldType::kUint));
  f2.put_varint(222);
  auto rec2 = make_record(1, f2.bytes());

  ByteWriter jw;
  // committed insert at (0,0)
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_set(jw, JournalOp::kInsert, 0, 0, rec1);
  put_journal_op(jw, JournalOp::kCommit);
  // rolled-back insert at (0,1)
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_set(jw, JournalOp::kInsert, 0, 1, rec2);
  put_journal_op(jw, JournalOp::kRollback);

  const std::uint32_t journal_offset =
      static_cast<std::uint32_t>(kHeaderSize + page0.size());
  auto hdr = make_header(page_size, 1, 0, kNoStringTable, journal_offset, 0,
                         nullptr);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, jw.bytes());

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.records.size(), static_cast<std::size_t>(1));
  BW_CHECK(db.records.count(RecordKey{0, 0}) == 1);
  BW_CHECK(db.records.count(RecordKey{0, 1}) == 0);
  BW_CHECK_EQ(db.records.at(RecordKey{0, 0}).fields[0].u64,
             static_cast<std::uint64_t>(111));
  BW_CHECK_EQ(db.rolled_back_ops, static_cast<std::uint32_t>(1));
}

// A committed INSERT followed by a committed DELETE removes the record.
BW_TEST(journal_delete_removes_record) {
  const std::uint16_t page_size = 64;
  auto page0 = make_page(PageType::kData, page_size, 0xFFFF, {});

  ByteWriter f1;
  f1.put_u8(static_cast<std::uint8_t>(FieldType::kBool));
  f1.put_u8(1);
  auto rec1 = make_record(1, f1.bytes());

  ByteWriter jw;
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_set(jw, JournalOp::kInsert, 0, 5, rec1);
  put_journal_op(jw, JournalOp::kCommit);
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_delete(jw, 0, 5);
  put_journal_op(jw, JournalOp::kCommit);

  const std::uint32_t journal_offset =
      static_cast<std::uint32_t>(kHeaderSize + page0.size());
  auto hdr = make_header(page_size, 1, 0, kNoStringTable, journal_offset, 0,
                         nullptr);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, jw.bytes());

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.records.size(), static_cast<std::size_t>(0));
}

// A wrong header CRC -> checksum_ok == false, but the DB still decodes.
BW_TEST(bad_header_crc_is_lenient) {
  const std::uint16_t page_size = 64;
  auto page0 = make_page(PageType::kData, page_size, 0xFFFF, {});

  const std::uint32_t wrong = 0xDEADBEEFu;
  auto hdr = make_header(page_size, 1, 0, kNoStringTable, 0, 0, &wrong);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);

  Database db = open(buf.data(), buf.size());
  BW_CHECK(!db.checksum_ok);
  BW_CHECK_EQ(db.pages.size(), static_cast<std::size_t>(1));

  // Strict mode promotes the same mismatch to a hard error.
  Options strict;
  strict.strict = true;
  BW_CHECK_THROWS(open(buf.data(), buf.size(), strict));
}

// Truncated input (shorter than the 32-byte header) -> kShortRead.
BW_TEST(truncated_header_short_read) {
  auto hdr = make_header(64, 1, 0, kNoStringTable, 0, 0, nullptr);
  hdr.resize(20);  // cut the header in half

  bool threw_short = false;
  try {
    open(hdr.data(), hdr.size());
  } catch (const bw::common::ParseError& e) {
    threw_short = (e.code() == bw::common::ErrorCode::kShortRead);
  }
  BW_CHECK(threw_short);
}

// A page_size that is not a multiple of 16 (and a too-small one) -> kBadLength.
BW_TEST(bad_page_size_bad_length) {
  auto hdr_misaligned = make_header(100, 1, 0, kNoStringTable, 0, 0, nullptr);
  bool threw_len = false;
  try {
    open(hdr_misaligned.data(), hdr_misaligned.size());
  } catch (const bw::common::ParseError& e) {
    threw_len = (e.code() == bw::common::ErrorCode::kBadLength);
  }
  BW_CHECK(threw_len);

  auto hdr_small = make_header(32, 1, 0, kNoStringTable, 0, 0, nullptr);
  bool threw_small = false;
  try {
    open(hdr_small.data(), hdr_small.size());
  } catch (const bw::common::ParseError& e) {
    threw_small = (e.code() == bw::common::ErrorCode::kBadLength);
  }
  BW_CHECK(threw_small);
}

// An unknown field type tag in a committed record -> kBadType.
BW_TEST(unknown_field_type_bad_type) {
  const std::uint16_t page_size = 64;
  auto page0 = make_page(PageType::kData, page_size, 0xFFFF, {});

  // A record claiming one field of type 99 (undefined).
  ByteWriter fields;
  fields.put_u8(99);
  auto rec = make_record(1, fields.bytes());

  ByteWriter jw;
  put_journal_op(jw, JournalOp::kBegin);
  put_journal_set(jw, JournalOp::kInsert, 0, 0, rec);
  put_journal_op(jw, JournalOp::kCommit);

  const std::uint32_t journal_offset =
      static_cast<std::uint32_t>(kHeaderSize + page0.size());
  auto hdr = make_header(page_size, 1, 0, kNoStringTable, journal_offset, 0,
                         nullptr);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, jw.bytes());

  bool threw_type = false;
  try {
    open(buf.data(), buf.size());
  } catch (const bw::common::ParseError& e) {
    threw_type = (e.code() == bw::common::ErrorCode::kBadType);
  }
  BW_CHECK(threw_type);
}

// A multi-page store with an overflow_next link parses and summarizes.
BW_TEST(multi_page_overflow_summary) {
  const std::uint16_t page_size = 64;
  auto page0 = make_page(PageType::kData, page_size, /*overflow_next=*/1, {});
  auto page1 = make_page(PageType::kOverflow, page_size, 0xFFFF, {});

  auto hdr = make_header(page_size, 2, 0, kNoStringTable, 0, 0, nullptr);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, page1);

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.pages.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(db.pages[0].overflow_next, static_cast<std::uint16_t>(1));
  BW_CHECK(db.pages[1].type == PageType::kOverflow);
  // summarize must run without throwing and produce content.
  BW_CHECK(summarize(db).size() > 0);
}

BW_TEST_MAIN()
