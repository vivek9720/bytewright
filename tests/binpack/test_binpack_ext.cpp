// bytewright - binpack extension tests (builder / visitor / validate / query)
//
// This translation unit adds BW_TEST cases for the encoder, the structural
// validator, the visitor walk, and the query helpers. It deliberately does NOT
// define BW_TEST_MAIN(): test_binpack.cpp already provides main(), and the
// inline registry in bw_test.hpp collects cases across every TU linked into the
// single test_binpack executable.
//
// The builder tests are the load-bearing ones: they assert that Builder output
// decodes through the real parse() with checksum_ok == true and the expected
// tree, proving byte-compatibility with the existing parser.
#include "bw_test.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "binpack/binpack.hpp"
#include "binpack/builder.hpp"
#include "binpack/query.hpp"
#include "binpack/visitor.hpp"
#include "common/status.hpp"

namespace {

using namespace bw::binpack;
using bw::common::ErrorCode;
using bw::common::ParseError;

// ---- builder round-trip -------------------------------------------------

BW_TEST(builder_roundtrips_through_parse_simple) {
  Builder b;
  b.set_has_metadata(false);
  b.add_section(SectionType::kData, /*id=*/7);
  b.add_record(RecordSpec::Uint(99));
  b.add_record(RecordSpec::Int(-7));
  b.add_record(RecordSpec::String("hello"));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  // Byte-compatible: every CRC the parser computes must match.
  BW_CHECK(c.checksum_ok);
  BW_CHECK(c.header_checksum_ok);
  BW_CHECK_EQ(c.version, 1);
  BW_CHECK_EQ(c.sections.size(), static_cast<std::size_t>(1));

  const Section& s = c.sections[0];
  BW_CHECK_EQ(s.type, SectionType::kData);
  BW_CHECK_EQ(s.id, static_cast<std::uint16_t>(7));
  BW_CHECK(s.body_checksum_ok);
  BW_CHECK_EQ(s.item_count, static_cast<std::uint32_t>(3));  // auto-filled
  BW_CHECK_EQ(s.records.size(), static_cast<std::size_t>(3));
  BW_CHECK_EQ(s.records[0].u64, static_cast<std::uint64_t>(99));
  BW_CHECK_EQ(s.records[1].i64, static_cast<std::int64_t>(-7));
  BW_CHECK_EQ(s.records[2].str, std::string("hello"));
}

BW_TEST(builder_roundtrips_nested_records) {
  // Mirror the existing parser test's structure: a GROUP holding an ARRAY of
  // two UINTs and a KEYVAL "name" -> STRING.
  Builder b;
  b.add_section(SectionType::kData, /*id=*/1);
  b.add_record(RecordSpec::Group({
      RecordSpec::Array({RecordSpec::Uint(10), RecordSpec::Uint(20)}),
      RecordSpec::Keyval("name", RecordSpec::String("fenrir")),
  }));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  BW_CHECK(c.checksum_ok);
  BW_CHECK_EQ(c.sections.size(), static_cast<std::size_t>(1));
  const Section& s = c.sections[0];
  BW_CHECK_EQ(s.records.size(), static_cast<std::size_t>(1));

  const Record& group = s.records[0];
  BW_CHECK_EQ(group.tag, RecordTag::kGroup);
  BW_CHECK(group.group);  // group flag round-trips
  BW_CHECK_EQ(group.children.size(), static_cast<std::size_t>(2));

  const Record& arr = group.children[0];
  BW_CHECK_EQ(arr.tag, RecordTag::kArray);
  BW_CHECK_EQ(arr.children.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(arr.children[0].u64, static_cast<std::uint64_t>(10));
  BW_CHECK_EQ(arr.children[1].u64, static_cast<std::uint64_t>(20));

  const Record& kv = group.children[1];
  BW_CHECK_EQ(kv.tag, RecordTag::kKeyval);
  BW_CHECK_EQ(kv.key, std::string("name"));
  BW_CHECK_EQ(kv.children.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(kv.children[0].str, std::string("fenrir"));

  // The whole thing must summarize without trouble.
  BW_CHECK(summarize(c).size() > 0);
}

BW_TEST(builder_float_and_blob_roundtrip) {
  Builder b;
  b.add_section(SectionType::kData);
  b.add_record(RecordSpec::Float(1.5));
  b.add_record(RecordSpec::Blob({0xDE, 0xAD, 0xBE, 0xEF}));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  BW_CHECK(c.checksum_ok);
  const Section& s = c.sections[0];
  BW_CHECK_EQ(s.records.size(), static_cast<std::size_t>(2));
  BW_CHECK(s.records[0].f64 == 1.5);
  BW_CHECK_EQ(s.records[1].tag, RecordTag::kBlob);
  BW_CHECK_EQ(s.records[1].blob.size(), static_cast<std::size_t>(4));
  BW_CHECK_EQ(s.records[1].blob[0], static_cast<std::uint8_t>(0xDE));
  BW_CHECK_EQ(s.records[1].blob[3], static_cast<std::uint8_t>(0xEF));
}

BW_TEST(builder_metadata_and_trailer_crc_roundtrip) {
  Builder b;
  b.set_has_metadata(true);
  b.set_has_trailer_crc(true);
  b.add_section(SectionType::kMetadata, /*id=*/2);
  b.add_record(RecordSpec::Keyval("author", RecordSpec::String("ada")));
  b.add_record(RecordSpec::Keyval("count", RecordSpec::Uint(42)));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  // Trailer + header + body CRCs all consistent.
  BW_CHECK(c.has_metadata);
  BW_CHECK(c.has_trailer_crc);
  BW_CHECK(c.trailer_checksum_ok);
  BW_CHECK(c.checksum_ok);

  // Metadata view collected by parse() from the METADATA section.
  BW_CHECK_EQ(c.metadata.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(c.metadata[0].first, std::string("author"));
  BW_CHECK_EQ(c.metadata[0].second.str, std::string("ada"));
  BW_CHECK_EQ(c.metadata[1].first, std::string("count"));
  BW_CHECK_EQ(c.metadata[1].second.u64, static_cast<std::uint64_t>(42));
}

BW_TEST(builder_multi_section_roundtrip) {
  Builder b;
  b.add_section(SectionType::kData, /*id=*/10);
  b.add_record(RecordSpec::Uint(1));
  b.add_section(SectionType::kStrings, /*id=*/11);
  b.add_record(RecordSpec::String("a"));
  b.add_record(RecordSpec::String("b"));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  BW_CHECK(c.checksum_ok);
  BW_CHECK_EQ(c.sections.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(c.sections[0].id, static_cast<std::uint16_t>(10));
  BW_CHECK_EQ(c.sections[0].records.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(c.sections[1].type, SectionType::kStrings);
  BW_CHECK_EQ(c.sections[1].records.size(), static_cast<std::size_t>(2));
}

// ---- validator ----------------------------------------------------------

BW_TEST(validate_clean_container_has_no_issues) {
  Builder b;
  b.add_section(SectionType::kData);
  b.add_record(RecordSpec::Group({RecordSpec::Uint(1)}));
  b.add_record(RecordSpec::Array({RecordSpec::Uint(2), RecordSpec::Uint(3)}));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  ValidationReport report = validate(c);
  BW_CHECK(report.ok());
  BW_CHECK_EQ(report.issues.size(), static_cast<std::size_t>(0));
}

BW_TEST(validate_catches_empty_group_and_array) {
  Builder b;
  b.add_section(SectionType::kData);
  b.add_record(RecordSpec::Group({}));   // empty group  -> warning
  b.add_record(RecordSpec::Array({}));   // empty array  -> warning

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  ValidationReport report = validate(c);
  // Two warnings, still "ok" because there are no errors.
  BW_CHECK(report.ok());
  BW_CHECK_EQ(report.warning_count(), static_cast<std::size_t>(2));

  bool saw_empty_group = false;
  bool saw_empty_array = false;
  for (const Issue& i : report.issues) {
    if (i.kind == IssueKind::kEmptyGroup) saw_empty_group = true;
    if (i.kind == IssueKind::kEmptyArray) saw_empty_array = true;
  }
  BW_CHECK(saw_empty_group);
  BW_CHECK(saw_empty_array);
}

BW_TEST(validate_catches_item_count_mismatch) {
  Builder b;
  b.add_section(SectionType::kData);
  b.add_record(RecordSpec::Uint(1));
  b.add_record(RecordSpec::Uint(2));
  // Lie about the count: declare 5 while only 2 records exist.
  b.set_item_count(5);

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());
  // Sanity: the section table really did record our bogus count.
  BW_CHECK_EQ(c.sections[0].item_count, static_cast<std::uint32_t>(5));

  ValidationReport report = validate(c);
  BW_CHECK(!report.ok());
  BW_CHECK_EQ(report.error_count(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(report.issues[0].kind, IssueKind::kItemCountMismatch);
  BW_CHECK_EQ(report.issues[0].section_index, static_cast<std::size_t>(0));
}

BW_TEST(validate_catches_oversized_blob) {
  Builder b;
  b.add_section(SectionType::kData);
  b.add_record(RecordSpec::Blob(std::vector<std::uint8_t>(64, 0xAB)));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  // Tighten the blob limit so the 64-byte blob trips it.
  ValidateOptions opts;
  opts.max_blob_bytes = 16;
  ValidationReport report = validate(c, opts);

  BW_CHECK_EQ(report.warning_count(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(report.issues[0].kind, IssueKind::kOversizedBlob);
  // Still "ok" -- oversized blobs are warnings, not errors.
  BW_CHECK(report.ok());
}

BW_TEST(validate_catches_duplicate_metadata_keys) {
  Builder b;
  b.set_has_metadata(true);
  b.add_section(SectionType::kMetadata, /*id=*/2);
  b.add_record(RecordSpec::Keyval("dup", RecordSpec::Uint(1)));
  b.add_record(RecordSpec::Keyval("dup", RecordSpec::Uint(2)));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());
  BW_CHECK_EQ(c.metadata.size(), static_cast<std::size_t>(2));

  ValidationReport report = validate(c);
  BW_CHECK(!report.ok());
  bool saw_dup = false;
  for (const Issue& i : report.issues) {
    if (i.kind == IssueKind::kDuplicateMetadataKey) saw_dup = true;
  }
  BW_CHECK(saw_dup);
}

// ---- visitor ------------------------------------------------------------

// Counts the structural events of a walk so we can assert the traversal shape.
class CountingVisitor : public RecordVisitor {
 public:
  std::size_t scalars = 0;
  std::size_t arrays = 0;
  std::size_t groups = 0;
  std::size_t keyvals = 0;
  std::size_t max_depth = 0;

  void visit_scalar(const Record&, std::size_t depth, std::size_t) override {
    ++scalars;
    if (depth > max_depth) max_depth = depth;
  }
  void enter_array(const Record&, std::size_t depth, std::size_t) override {
    ++arrays;
    if (depth > max_depth) max_depth = depth;
  }
  void enter_group(const Record&, std::size_t depth, std::size_t) override {
    ++groups;
    if (depth > max_depth) max_depth = depth;
  }
  void enter_keyval(const Record&, std::size_t depth, std::size_t) override {
    ++keyvals;
    if (depth > max_depth) max_depth = depth;
  }
};

BW_TEST(visitor_walks_full_tree) {
  Builder b;
  b.add_section(SectionType::kData);
  // group { array[uint, uint], keyval "k" -> string }
  b.add_record(RecordSpec::Group({
      RecordSpec::Array({RecordSpec::Uint(1), RecordSpec::Uint(2)}),
      RecordSpec::Keyval("k", RecordSpec::String("v")),
  }));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  CountingVisitor v;
  walk(c, v);

  BW_CHECK_EQ(v.groups, static_cast<std::size_t>(1));
  BW_CHECK_EQ(v.arrays, static_cast<std::size_t>(1));
  BW_CHECK_EQ(v.keyvals, static_cast<std::size_t>(1));
  // Two array elements + one keyval value string = 3 scalars.
  BW_CHECK_EQ(v.scalars, static_cast<std::size_t>(3));
  // Deepest scalars (array elements / keyval value) sit at depth 2.
  BW_CHECK_EQ(v.max_depth, static_cast<std::size_t>(2));
}

// ---- query --------------------------------------------------------------

BW_TEST(query_find_section_and_metadata_lookup) {
  Builder b;
  b.set_has_metadata(true);
  b.add_section(SectionType::kData, /*id=*/10);
  b.add_record(RecordSpec::Uint(7));
  b.add_section(SectionType::kMetadata, /*id=*/2);
  b.add_record(RecordSpec::Keyval("author", RecordSpec::String("ada")));
  b.add_record(RecordSpec::Keyval("count", RecordSpec::Uint(42)));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  const Section* data = find_section(c, SectionType::kData);
  BW_CHECK(data != nullptr);
  BW_CHECK_EQ(data->id, static_cast<std::uint16_t>(10));

  const Section* meta = find_section(c, SectionType::kMetadata);
  BW_CHECK(meta != nullptr);

  const Section* by_id = find_section_by_id(c, 10);
  BW_CHECK(by_id != nullptr);
  BW_CHECK_EQ(by_id->type, SectionType::kData);

  // Missing section/id -> nullptr.
  BW_CHECK(find_section(c, SectionType::kIndex) == nullptr);
  BW_CHECK(find_section_by_id(c, 999) == nullptr);

  // Metadata lookups.
  const Record* author = metadata_value(c, "author");
  BW_CHECK(author != nullptr);
  BW_CHECK_EQ(author->tag, RecordTag::kString);

  auto author_str = metadata_string(c, "author");
  BW_CHECK(author_str.has_value());
  BW_CHECK_EQ(author_str.value(), std::string("ada"));

  auto count = metadata_uint(c, "count");
  BW_CHECK(count.has_value());
  BW_CHECK_EQ(count.value(), static_cast<std::uint64_t>(42));

  // Wrong-type / missing accessors -> nullopt.
  BW_CHECK(!metadata_uint(c, "author").has_value());
  BW_CHECK(!metadata_string(c, "missing").has_value());
  BW_CHECK(metadata_value(c, "missing") == nullptr);
}

BW_TEST(query_collect_records_recursive_and_shallow) {
  Builder b;
  b.add_section(SectionType::kData);
  // Top-level uint, plus a group containing two more uints and a string.
  b.add_record(RecordSpec::Uint(1));
  b.add_record(RecordSpec::Group({
      RecordSpec::Uint(2),
      RecordSpec::Uint(3),
      RecordSpec::String("s"),
  }));

  std::vector<std::uint8_t> bytes = b.build();
  Container c = parse(bytes.data(), bytes.size());

  // Recursive: the top-level uint plus the two inside the group = 3.
  std::vector<const Record*> uints = collect_records(c, RecordTag::kUint, true);
  BW_CHECK_EQ(uints.size(), static_cast<std::size_t>(3));

  // Shallow: only the top-level uint.
  std::vector<const Record*> shallow =
      collect_records(c, RecordTag::kUint, false);
  BW_CHECK_EQ(shallow.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(shallow[0]->u64, static_cast<std::uint64_t>(1));

  // Strings: one, inside the group (recursive).
  std::vector<const Record*> strings =
      collect_records(c, RecordTag::kString, true);
  BW_CHECK_EQ(strings.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(strings[0]->str, std::string("s"));

  // Per-section overload agrees with the container-wide one.
  std::vector<const Record*> per_section =
      collect_records(c.sections[0], RecordTag::kUint, true);
  BW_CHECK_EQ(per_section.size(), static_cast<std::size_t>(3));
}

}  // namespace
