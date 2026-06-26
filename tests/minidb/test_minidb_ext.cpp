// bytewright - minidb extension-module unit tests.
//
// Covers the four modules added on top of the core decoder: the INDEX page
// decoder + lookup (btree), the committed-record Cursor (cursor), overflow-chain
// reconstruction with cycle/length guards (overflow), and the Catalog roll-up
// (catalog). Inputs are assembled with the shared ByteWriter, mirroring the
// builder style of test_minidb.cpp. This file deliberately has NO BW_TEST_MAIN()
// - test_minidb.cpp provides main() and the shared registry collects every case.
#include <cstdint>
#include <string>
#include <vector>

#include "bw_test.hpp"
#include "common/byte_writer.hpp"
#include "common/checksum.hpp"
#include "minidb/btree.hpp"
#include "minidb/catalog.hpp"
#include "minidb/cursor.hpp"
#include "minidb/format.hpp"
#include "minidb/minidb.hpp"
#include "minidb/model.hpp"
#include "minidb/overflow.hpp"

namespace {

using bw::common::ByteWriter;
using namespace bw::minidb;

// ---- builders (local to this TU; mirror test_minidb.cpp's helpers) ---------

// 32-byte file header with a correct CRC over the first 28 bytes.
std::vector<std::uint8_t> make_header(std::uint16_t page_size,
                                      std::uint32_t page_count,
                                      std::uint32_t root_page,
                                      std::uint32_t string_table_page,
                                      std::uint32_t journal_offset,
                                      std::uint32_t flags) {
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
  w.put_u32le(bw::common::crc32(w.bytes().data(), 28));
  return w.bytes();
}

// A record body: field_count varint then the raw field bytes.
std::vector<std::uint8_t> make_record(std::uint64_t field_count,
                                      const std::vector<std::uint8_t>& fields) {
  ByteWriter w;
  w.put_varint(field_count);
  w.put_bytes(fields);
  return w.bytes();
}

// A DATA/STRINGS/etc. page image of exactly page_size bytes, with records laid
// out right after the slot directory. Same layout the core decoder expects.
std::vector<std::uint8_t> make_page(
    PageType type, std::uint16_t page_size, std::uint16_t overflow_next,
    const std::vector<std::vector<std::uint8_t>>& records) {
  const std::uint16_t slot_count = static_cast<std::uint16_t>(records.size());
  std::vector<std::uint8_t> page(page_size, 0);
  std::size_t cursor = kPageHeaderSize +
                       static_cast<std::size_t>(slot_count) * kSlotEntrySize;

  page[0] = static_cast<std::uint8_t>(type);
  page[1] = 0;
  page[2] = static_cast<std::uint8_t>(slot_count & 0xFF);
  page[3] = static_cast<std::uint8_t>((slot_count >> 8) & 0xFF);
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

// Build an INDEX page image: 8-byte header (type=INDEX), then the index body
// (entry_count varint, then key_len varint + key + page u32 + slot u16 each).
// `free_start` is set to span the whole written body so payload-region logic in
// other modules behaves, though btree decoding does not depend on it.
std::vector<std::uint8_t> make_index_page(
    std::uint16_t page_size, std::uint16_t overflow_next,
    const std::vector<IndexEntry>& entries) {
  std::vector<std::uint8_t> page(page_size, 0);

  // Body, assembled separately so we can copy it after the header.
  ByteWriter body;
  body.put_varint(entries.size());
  for (const IndexEntry& e : entries) {
    body.put_varint(e.key.size());
    body.put_string(e.key);
    body.put_u32le(e.page);
    body.put_u16le(e.slot);
  }
  const std::vector<std::uint8_t>& bbytes = body.bytes();

  page[0] = static_cast<std::uint8_t>(PageType::kIndex);
  page[1] = 0;
  // slot_count left 0 (the core decoder reads no slots for an INDEX page).
  page[6] = static_cast<std::uint8_t>(overflow_next & 0xFF);
  page[7] = static_cast<std::uint8_t>((overflow_next >> 8) & 0xFF);

  for (std::size_t i = 0; i < bbytes.size() && kPageHeaderSize + i < page.size();
       ++i) {
    page[kPageHeaderSize + i] = bbytes[i];
  }
  const std::uint16_t free_start =
      static_cast<std::uint16_t>(kPageHeaderSize + bbytes.size());
  page[4] = static_cast<std::uint8_t>(free_start & 0xFF);
  page[5] = static_cast<std::uint8_t>((free_start >> 8) & 0xFF);
  return page;
}

// An OVERFLOW page carrying `payload` raw bytes right after the 8-byte header,
// with free_start set to the end of the payload and the given overflow_next link.
std::vector<std::uint8_t> make_overflow_page(
    std::uint16_t page_size, std::uint16_t overflow_next,
    const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> page(page_size, 0);
  page[0] = static_cast<std::uint8_t>(PageType::kOverflow);
  page[6] = static_cast<std::uint8_t>(overflow_next & 0xFF);
  page[7] = static_cast<std::uint8_t>((overflow_next >> 8) & 0xFF);
  for (std::size_t i = 0;
       i < payload.size() && kPageHeaderSize + i < page.size(); ++i) {
    page[kPageHeaderSize + i] = payload[i];
  }
  const std::uint16_t free_start =
      static_cast<std::uint16_t>(kPageHeaderSize + payload.size());
  page[4] = static_cast<std::uint8_t>(free_start & 0xFF);
  page[5] = static_cast<std::uint8_t>((free_start >> 8) & 0xFF);
  return page;
}

void append(std::vector<std::uint8_t>& into,
            const std::vector<std::uint8_t>& bytes) {
  into.insert(into.end(), bytes.begin(), bytes.end());
}

// A one-field UINT record, used to seed DATA pages for cursor/catalog tests.
std::vector<std::uint8_t> uint_record(std::uint64_t v) {
  ByteWriter f;
  f.put_u8(static_cast<std::uint8_t>(FieldType::kUint));
  f.put_varint(v);
  return make_record(1, f.bytes());
}

}  // namespace

// ---- INDEX page decode + ordered lookup ------------------------------------

// An INDEX page with out-of-order keys decodes into sorted entries; lookup finds
// exact keys, misses absent ones, and a range scan returns the inclusive window.
BW_TEST(index_page_decode_lookup_and_range) {
  const std::uint16_t page_size = 256;
  // Deliberately unsorted input keys; the decoder must sort them.
  std::vector<IndexEntry> entries = {
      {std::string("mango"), 4, 1},
      {std::string("apple"), 2, 0},
      {std::string("cherry"), 3, 7},
      {std::string("banana"), 9, 2},
  };
  auto page = make_index_page(page_size, kNoOverflow, entries);

  IndexPage idx = decode_index_page(page.data(), page.size(), /*page_index=*/0);
  BW_CHECK(!idx.truncated);
  BW_CHECK_EQ(idx.entries.size(), static_cast<std::size_t>(4));

  // Entries are sorted ascending by key.
  BW_CHECK_EQ(idx.entries[0].key, std::string("apple"));
  BW_CHECK_EQ(idx.entries[1].key, std::string("banana"));
  BW_CHECK_EQ(idx.entries[2].key, std::string("cherry"));
  BW_CHECK_EQ(idx.entries[3].key, std::string("mango"));

  // Point lookups resolve to the right locator.
  const IndexEntry* hit = idx.lookup("cherry");
  BW_CHECK(hit != nullptr);
  BW_CHECK_EQ(hit->page, static_cast<std::uint32_t>(3));
  BW_CHECK_EQ(hit->slot, static_cast<std::uint16_t>(7));

  const IndexEntry* first = idx.lookup("apple");
  BW_CHECK(first != nullptr);
  BW_CHECK_EQ(first->page, static_cast<std::uint32_t>(2));

  // A missing key returns nullptr.
  BW_CHECK(idx.lookup("durian") == nullptr);
  BW_CHECK(idx.lookup("zucchini") == nullptr);

  // Inclusive range [banana, mango] -> banana, cherry, mango (3 of 4).
  std::vector<IndexEntry> win = idx.range("banana", "mango");
  BW_CHECK_EQ(win.size(), static_cast<std::size_t>(3));
  BW_CHECK_EQ(win.front().key, std::string("banana"));
  BW_CHECK_EQ(win.back().key, std::string("mango"));

  // An inverted range selects nothing.
  BW_CHECK_EQ(idx.range("mango", "apple").size(), static_cast<std::size_t>(0));
}

// A non-INDEX page yields an empty index; a body truncated mid-entry keeps the
// clean prefix and flags truncated.
BW_TEST(index_page_non_index_and_truncated) {
  const std::uint16_t page_size = 128;

  // A DATA page fed to the index decoder produces no entries (and is not flagged
  // truncated - it is simply the wrong type).
  auto data_page = make_page(PageType::kData, page_size, kNoOverflow, {});
  IndexPage not_idx =
      decode_index_page(data_page.data(), data_page.size(), 0);
  BW_CHECK_EQ(not_idx.entries.size(), static_cast<std::size_t>(0));
  BW_CHECK(!not_idx.truncated);

  // Build an INDEX page that claims 3 entries but cut the buffer so only the
  // first two fit; decoding keeps two and flags truncated.
  std::vector<IndexEntry> entries = {
      {std::string("aa"), 1, 0},
      {std::string("bb"), 2, 0},
      {std::string("cc"), 3, 0},
  };
  auto page = make_index_page(page_size, kNoOverflow, entries);
  // free_start (bytes 4..5) marks the real end of the body; cut the page there
  // minus the last entry's worth of bytes (key_len 1 + key 2 + page 4 + slot 2 =
  // 9 bytes). Truncating the contiguous buffer simulates a short page.
  const std::size_t free_start =
      static_cast<std::size_t>(page[4]) |
      (static_cast<std::size_t>(page[5]) << 8);
  page.resize(free_start - 9);

  IndexPage idx = decode_index_page(page.data(), page.size(), 0);
  BW_CHECK(idx.truncated);
  BW_CHECK_EQ(idx.entries.size(), static_cast<std::size_t>(2));
  BW_CHECK(idx.lookup("aa") != nullptr);
  BW_CHECK(idx.lookup("cc") == nullptr);  // the dropped third entry
}

// ---- Cursor iteration over committed records -------------------------------

// A DB seeded with three DATA-page records iterates in (page, slot) order via the
// Cursor, exposes fields, and seek() positions correctly. project_row renders.
BW_TEST(cursor_iterates_committed_records) {
  const std::uint16_t page_size = 128;
  // page 0 carries two records (slots 0,1); page 1 carries one (slot 0).
  auto page0 = make_page(PageType::kData, page_size, kNoOverflow,
                         {uint_record(10), uint_record(20)});
  auto page1 = make_page(PageType::kData, page_size, kNoOverflow,
                         {uint_record(30)});
  auto hdr = make_header(page_size, 2, 0, kNoStringTable, 0, 0);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, page1);

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.records.size(), static_cast<std::size_t>(3));

  Cursor cur(db);
  BW_CHECK(!cur.valid());  // not started yet

  // First record: (0,0) -> 10.
  BW_CHECK(cur.next());
  BW_CHECK(cur.valid());
  BW_CHECK_EQ(cur.page(), static_cast<std::uint32_t>(0));
  BW_CHECK_EQ(cur.slot(), static_cast<std::uint16_t>(0));
  const Field* f0 = cur.field(0);
  BW_CHECK(f0 != nullptr);
  BW_CHECK_EQ(f0->u64, static_cast<std::uint64_t>(10));
  BW_CHECK(cur.field(5) == nullptr);  // out-of-range field index

  // Second: (0,1) -> 20.
  BW_CHECK(cur.next());
  BW_CHECK_EQ(cur.key(), (RecordKey{0, 1}));
  BW_CHECK_EQ(cur.field(0)->u64, static_cast<std::uint64_t>(20));

  // Third: (1,0) -> 30.
  BW_CHECK(cur.next());
  BW_CHECK_EQ(cur.page(), static_cast<std::uint32_t>(1));
  BW_CHECK_EQ(cur.field(0)->u64, static_cast<std::uint64_t>(30));

  // Exhausted.
  BW_CHECK(!cur.next());
  BW_CHECK(!cur.valid());

  // seek() jumps directly onto (1,0).
  Cursor s(db);
  BW_CHECK(s.seek(1, 0));
  BW_CHECK(s.valid());
  BW_CHECK_EQ(s.key(), (RecordKey{1, 0}));
  BW_CHECK_EQ(s.field(0)->u64, static_cast<std::uint64_t>(30));

  // A seek past the last key is invalid.
  Cursor past(db);
  BW_CHECK(!past.seek(99, 0));
  BW_CHECK(!past.valid());

  // project_row renders the single UINT field as its decimal text.
  std::vector<std::string> row = project_row(db.records.at(RecordKey{0, 1}));
  BW_CHECK_EQ(row.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(row[0], std::string("20"));
}

// ---- Overflow chain reconstruction -----------------------------------------

// A 0 -> 1 -> 2 overflow chain reconstructs the concatenated payloads and reports
// COMPLETE; a chain that loops back is caught by the cycle guard.
BW_TEST(overflow_chain_reconstruct_and_cycle_guard) {
  const std::uint16_t page_size = 64;

  // Three linked overflow pages with distinct payloads; page 2 terminates.
  std::vector<std::uint8_t> p0 = {0x01, 0x02, 0x03};
  std::vector<std::uint8_t> p1 = {0x04, 0x05};
  std::vector<std::uint8_t> p2 = {0x06, 0x07, 0x08, 0x09};
  auto page0 = make_overflow_page(page_size, /*overflow_next=*/1, p0);
  auto page1 = make_overflow_page(page_size, /*overflow_next=*/2, p1);
  auto page2 = make_overflow_page(page_size, kNoOverflow, p2);
  auto hdr = make_header(page_size, 3, 0, kNoStringTable, 0, 0);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, page1);
  append(buf, page2);

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.pages.size(), static_cast<std::size_t>(3));

  OverflowResult res = reconstruct_overflow(buf.data(), buf.size(), db, 0);
  BW_CHECK(res.status == OverflowStatus::kComplete);
  BW_CHECK_EQ(res.visited.size(), static_cast<std::size_t>(3));
  // Concatenated payload is p0 + p1 + p2 = 9 bytes.
  BW_CHECK_EQ(res.bytes.size(), static_cast<std::size_t>(9));
  BW_CHECK_EQ(res.bytes[0], static_cast<std::uint8_t>(0x01));
  BW_CHECK_EQ(res.bytes[3], static_cast<std::uint8_t>(0x04));
  BW_CHECK_EQ(res.bytes[5], static_cast<std::uint8_t>(0x06));
  BW_CHECK_EQ(res.bytes[8], static_cast<std::uint8_t>(0x09));

  // Now build a cyclic chain: 0 -> 1 -> 0. The walk must stop with kCycle and
  // must not loop forever.
  auto c0 = make_overflow_page(page_size, /*overflow_next=*/1, p0);
  auto c1 = make_overflow_page(page_size, /*overflow_next=*/0, p1);  // back to 0
  auto chdr = make_header(page_size, 2, 0, kNoStringTable, 0, 0);

  std::vector<std::uint8_t> cbuf;
  append(cbuf, chdr);
  append(cbuf, c0);
  append(cbuf, c1);

  Database cdb = open(cbuf.data(), cbuf.size());
  OverflowResult cyc = reconstruct_overflow(cbuf.data(), cbuf.size(), cdb, 0);
  BW_CHECK(cyc.status == OverflowStatus::kCycle);
  // It walked both pages once before detecting the loop on the third hop.
  BW_CHECK_EQ(cyc.visited.size(), static_cast<std::size_t>(2));

  // A length guard trips when the cap is below the chain's total payload.
  OverflowLimits tight;
  tight.max_bytes = 4;  // p0(3) fits, p0+p1(5) does not
  OverflowResult capped =
      reconstruct_overflow(buf.data(), buf.size(), db, 0, tight);
  BW_CHECK(capped.status == OverflowStatus::kLengthLimit);

  // An out-of-range start page is reported, not crashed.
  OverflowResult bad = reconstruct_overflow(buf.data(), buf.size(), db, 99);
  BW_CHECK(bad.status == OverflowStatus::kBadStart);
}

// ---- Catalog histogram + stats ---------------------------------------------

// A DB with a STRINGS page, two DATA pages (3 records total), and an INDEX page
// produces a correct page-type histogram, live-record count, and string stats.
BW_TEST(catalog_histogram_and_stats) {
  const std::uint16_t page_size = 256;

  // page 0: STRINGS with three entries (one empty).
  ByteWriter sw;
  std::vector<std::string> strs = {"alpha", "", "gamma"};
  sw.put_varint(strs.size());
  for (const auto& e : strs) {
    sw.put_varint(e.size());
    sw.put_string(e);
  }
  auto page0 = make_page(PageType::kStrings, page_size, kNoOverflow,
                         {sw.bytes()});

  // page 1: DATA with two records.
  auto page1 = make_page(PageType::kData, page_size, kNoOverflow,
                         {uint_record(1), uint_record(2)});
  // page 2: DATA with one record.
  auto page2 = make_page(PageType::kData, page_size, kNoOverflow,
                         {uint_record(3)});
  // page 3: INDEX (no records, but counted in the histogram).
  auto page3 = make_index_page(page_size, kNoOverflow,
                               {{std::string("k"), 1, 0}});

  auto hdr = make_header(page_size, 4, 0, /*string_table_page=*/0, 0, 0);

  std::vector<std::uint8_t> buf;
  append(buf, hdr);
  append(buf, page0);
  append(buf, page1);
  append(buf, page2);
  append(buf, page3);

  Database db = open(buf.data(), buf.size());
  BW_CHECK_EQ(db.records.size(), static_cast<std::size_t>(3));
  BW_CHECK_EQ(db.string_table.size(), static_cast<std::size_t>(3));

  Catalog cat = build_catalog(db);
  BW_CHECK_EQ(cat.total_pages, static_cast<std::size_t>(4));
  BW_CHECK_EQ(cat.count_of(PageType::kStrings), static_cast<std::size_t>(1));
  BW_CHECK_EQ(cat.count_of(PageType::kData), static_cast<std::size_t>(2));
  BW_CHECK_EQ(cat.count_of(PageType::kIndex), static_cast<std::size_t>(1));
  BW_CHECK_EQ(cat.count_of(PageType::kOverflow), static_cast<std::size_t>(0));
  BW_CHECK_EQ(cat.total_live_records, static_cast<std::size_t>(3));

  // Three used slots across the two DATA pages (2 + 1); the STRINGS page's single
  // slot is in-bounds and non-empty, so it counts too -> 4 used total.
  BW_CHECK_EQ(cat.total_used_slots, static_cast<std::size_t>(4));

  // String stats: 3 entries, bytes = 5 ("alpha") + 0 + 5 ("gamma") = 10, max 5,
  // one empty.
  BW_CHECK_EQ(cat.strings.count, static_cast<std::size_t>(3));
  BW_CHECK_EQ(cat.strings.total_bytes, static_cast<std::size_t>(10));
  BW_CHECK_EQ(cat.strings.max_bytes, static_cast<std::size_t>(5));
  BW_CHECK_EQ(cat.strings.empty_count, static_cast<std::size_t>(1));

  // format_catalog runs and yields content.
  BW_CHECK(format_catalog(cat).size() > 0);
}
