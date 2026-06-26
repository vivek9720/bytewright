// bytewright - minidb top-level facade: open() (parse + replay) and summarize().
//
// open() chains the decode stages declared in decoder.hpp:
//   1. parse_header        (hard gate: magic, version, page_size)
//   2. parse_pages         (page array + slot directories, lenient on EOF)
//   3. parse_string_table  (STRINGS page -> vector<string>)
//   4. replay_journal      (WAL replay -> committed records, decoded fields)
//
// Only stage 1 may throw on a near-valid input; the rest degrade gracefully so a
// fuzzer reaches every stage. decode_record (used by stage 4 and reachable for
// page-resident records via the journal store) throws kBadType on a bad tag.
#include "minidb/minidb.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "common/byte_reader.hpp"
#include "common/hexdump.hpp"
#include "minidb/decoder.hpp"

namespace bw {
namespace minidb {

Database open(const std::uint8_t* data, std::size_t size, const Options& opts) {
  common::ByteReader reader(data, size);

  Database db;

  const FileHeader hdr = parse_header(reader, opts.strict);
  db.version = hdr.version;
  db.page_size = hdr.page_size;
  db.page_count = hdr.page_count;
  db.root_page = hdr.root_page;
  db.string_table_page = hdr.string_table_page;
  db.journal_offset = hdr.journal_offset;
  db.flags = hdr.flags;
  db.checksum_ok = hdr.checksum_ok;

  const std::vector<PageSpan> spans = parse_pages(data, size, hdr, db);
  parse_string_table(data, size, hdr, spans, db);
  replay_journal(data, size, hdr, spans, db);

  return db;
}

Database open(const std::uint8_t* data, std::size_t size) {
  return open(data, size, Options{});
}

namespace {

// Render a single decoded field's value to a compact, single-line form.
std::string field_value(const Field& f) {
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
      if (f.ref_resolved) {
        return "#" + std::to_string(f.ref_id) + " \"" +
               common::escape(f.str) + "\"";
      }
      return "#" + std::to_string(f.ref_id) + " <unresolved>";
    case FieldType::kInlineString:
      return "\"" + common::escape(f.str) + "\"";
    case FieldType::kBlob:
      return "blob[" + std::to_string(f.blob.size()) + "]";
  }
  return "?";
}

}  // namespace

std::string summarize(const Database& db) {
  std::string out;
  out += "minidb MDB1 store\n";
  out += "  version       : " + std::to_string(db.version) + "\n";
  out += "  page_size     : " + std::to_string(db.page_size) + "\n";
  out += "  page_count    : " + std::to_string(db.page_count) + "\n";
  out += "  root_page     : " + std::to_string(db.root_page) + "\n";
  if (db.string_table_page == kNoStringTable) {
    out += "  string_table  : (none)\n";
  } else {
    out += "  string_table  : page " +
           std::to_string(db.string_table_page) + "\n";
  }
  out += "  journal       : ";
  if (!db.has_journal) {
    out += "(none)\n";
  } else {
    out += "offset " + std::to_string(db.journal_offset) + ", " +
           std::to_string(db.committed_ops) + " committed, " +
           std::to_string(db.rolled_back_ops) + " rolled back\n";
  }
  out += "  flags         : " + common::to_hex(db.flags, 8) + "\n";
  out += "  checksum_ok   : ";
  out += db.checksum_ok ? "yes\n" : "no\n";

  // String table.
  out += "string table (" + std::to_string(db.string_table.size()) +
         " entries)\n";
  for (std::size_t i = 0; i < db.string_table.size(); ++i) {
    out += "  [" + std::to_string(i) + "] \"" +
           common::escape(db.string_table[i]) + "\"\n";
  }

  // Pages.
  out += "pages (" + std::to_string(db.pages.size()) + ")\n";
  for (const Page& p : db.pages) {
    out += "  page " + std::to_string(p.index) + ": " +
           page_type_name(p.type) + ", slots=" +
           std::to_string(p.slot_count);
    if (p.overflow_next != kNoOverflow) {
      out += ", overflow_next=" + std::to_string(p.overflow_next);
    }
    if (p.truncated) {
      out += ", truncated";
    }
    out += "\n";
  }

  // Final committed records, with resolved field values.
  out += "committed records (" + std::to_string(db.records.size()) + ")\n";
  for (const auto& kv : db.records) {
    out += "  (page " + std::to_string(kv.first.first) + ", slot " +
           std::to_string(kv.first.second) + "): ";
    const Record& rec = kv.second;
    for (std::size_t i = 0; i < rec.fields.size(); ++i) {
      if (i != 0) {
        out += ", ";
      }
      out += field_type_name(rec.fields[i].type);
      out += "=";
      out += field_value(rec.fields[i]);
    }
    out += "\n";
  }

  return out;
}

}  // namespace minidb
}  // namespace bw
