// bytewright - binpack unit tests
//
// Inputs are assembled with common::ByteWriter and back-patched for CRCs and
// lengths, mirroring how a real producer would emit the format. Each test
// builds a container, then asserts on the decoded structure or on the thrown
// error code.
#include "bw_test.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "binpack/binpack.hpp"
#include "common/byte_writer.hpp"
#include "common/checksum.hpp"
#include "common/status.hpp"

namespace {

using bw::common::ByteWriter;
using bw::common::ErrorCode;
using bw::common::ParseError;
using namespace bw::binpack;

// ---- small encoders mirroring the wire format --------------------------

// Append a record header + already-encoded payload bytes.
void put_record(ByteWriter& w, std::uint8_t tag, std::uint8_t flags,
                const std::vector<std::uint8_t>& payload) {
  w.put_u8(tag);
  w.put_u8(flags);
  w.put_varint(payload.size());
  w.put_bytes(payload);
}

std::vector<std::uint8_t> enc_uint(std::uint64_t v) {
  ByteWriter w;
  w.put_varint(v);
  return w.bytes();
}

std::vector<std::uint8_t> enc_int(std::int64_t v) {
  ByteWriter w;
  w.put_svarint(v);
  return w.bytes();
}

std::vector<std::uint8_t> enc_string(const std::string& s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

// A KEYVAL payload: key_len varint + key + one nested record.
std::vector<std::uint8_t> enc_keyval(const std::string& key, std::uint8_t vtag,
                                     const std::vector<std::uint8_t>& vpayload) {
  ByteWriter w;
  w.put_varint(key.size());
  w.put_string(key);
  put_record(w, vtag, 0x00, vpayload);
  return w.bytes();
}

// Build a complete one-section container. `section_type` and the already-built
// body bytes are supplied; CRCs are computed and patched. If break_body_crc is
// set, the section CRC is written wrong on purpose.
std::vector<std::uint8_t> build_container(
    std::uint16_t section_type, std::uint32_t item_count,
    const std::vector<std::uint8_t>& body, std::uint16_t flags = 0,
    bool break_body_crc = false) {
  ByteWriter w;
  // Header (12 bytes covered by header_crc).
  w.put_u8('B'); w.put_u8('P'); w.put_u8('K'); w.put_u8('1');
  w.put_u16le(1);            // version
  w.put_u16le(flags);        // flags
  w.put_u16le(1);            // section_count
  w.put_u16le(0);            // reserved
  std::uint32_t header_crc = bw::common::crc32(w.bytes().data(), 12);
  w.put_u32le(header_crc);

  // Section table entry (24 bytes). offset is patched once we know it.
  w.put_u16le(section_type);
  w.put_u16le(7);            // id
  w.put_u16le(0);            // flags
  w.put_u16le(0);            // reserved
  std::size_t offset_slot = w.reserve_u32le();
  w.put_u32le(static_cast<std::uint32_t>(body.size()));  // length
  w.put_u32le(item_count);
  std::uint32_t body_crc = bw::common::crc32(body.data(), body.size());
  if (break_body_crc) body_crc ^= 0xFFFFFFFFu;
  w.put_u32le(body_crc);

  std::uint32_t body_offset = static_cast<std::uint32_t>(w.size());
  w.patch_u32le(offset_slot, body_offset);
  w.put_bytes(body);

  return w.bytes();
}

// ---- tests --------------------------------------------------------------

BW_TEST(valid_nested_group_array_keyval) {
  // Body: a GROUP containing [ an ARRAY of two UINTs, a KEYVAL "k"->STRING ].
  ByteWriter array_w;
  array_w.put_varint(2);                       // array count
  put_record(array_w, 0x02, 0x00, enc_uint(10));
  put_record(array_w, 0x02, 0x00, enc_uint(20));

  ByteWriter group_w;
  // nested array record
  put_record(group_w, 0x06, 0x00, array_w.bytes());
  // keyval record
  put_record(group_w, 0x08, 0x00,
             enc_keyval("name", 0x04, enc_string("fenrir")));

  ByteWriter body_w;
  put_record(body_w, 0x07, 0x01 /*group flag*/, group_w.bytes());

  auto bytes = build_container(/*DATA*/ 1, /*item_count*/ 1, body_w.bytes());
  Container c = parse(bytes.data(), bytes.size());

  BW_CHECK_EQ(c.version, 1);
  BW_CHECK(c.checksum_ok);
  BW_CHECK_EQ(c.sections.size(), static_cast<std::size_t>(1));

  const Section& s = c.sections[0];
  BW_CHECK_EQ(s.type, SectionType::kData);
  BW_CHECK_EQ(s.records.size(), static_cast<std::size_t>(1));

  const Record& group = s.records[0];
  BW_CHECK_EQ(group.tag, RecordTag::kGroup);
  BW_CHECK(group.group);
  BW_CHECK_EQ(group.children.size(), static_cast<std::size_t>(2));

  const Record& arr = group.children[0];
  BW_CHECK_EQ(arr.tag, RecordTag::kArray);
  BW_CHECK_EQ(arr.children.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(arr.children[0].tag, RecordTag::kUint);
  BW_CHECK_EQ(arr.children[0].u64, static_cast<std::uint64_t>(10));
  BW_CHECK_EQ(arr.children[1].u64, static_cast<std::uint64_t>(20));

  const Record& kv = group.children[1];
  BW_CHECK_EQ(kv.tag, RecordTag::kKeyval);
  BW_CHECK_EQ(kv.key, std::string("name"));
  BW_CHECK_EQ(kv.children.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(kv.children[0].tag, RecordTag::kString);
  BW_CHECK_EQ(kv.children[0].str, std::string("fenrir"));

  // summarize must run and produce non-trivial output.
  BW_CHECK(summarize(c).size() > 0);
}

BW_TEST(metadata_section_collects_keyvals) {
  ByteWriter body_w;
  put_record(body_w, 0x08, 0x00, enc_keyval("author", 0x04, enc_string("ada")));
  put_record(body_w, 0x08, 0x00, enc_keyval("count", 0x02, enc_uint(42)));

  // flags bit0 = has_metadata (advisory); section type METADATA = 2.
  auto bytes = build_container(/*METADATA*/ 2, 2, body_w.bytes(), /*flags*/ 0x01);
  Container c = parse(bytes.data(), bytes.size());

  BW_CHECK(c.checksum_ok);
  BW_CHECK_EQ(c.metadata.size(), static_cast<std::size_t>(2));
  BW_CHECK_EQ(c.metadata[0].first, std::string("author"));
  BW_CHECK_EQ(c.metadata[0].second.str, std::string("ada"));
  BW_CHECK_EQ(c.metadata[1].first, std::string("count"));
  BW_CHECK_EQ(c.metadata[1].second.u64, static_cast<std::uint64_t>(42));
}

BW_TEST(float_and_int_roundtrip) {
  ByteWriter fbody;
  // FLOAT 8 bytes LE for 1.5 -> bits 0x3FF8000000000000.
  std::uint64_t bits = 0x3FF8000000000000ull;
  std::vector<std::uint8_t> fbytes(8);
  for (int i = 0; i < 8; ++i) fbytes[i] = static_cast<std::uint8_t>(bits >> (8 * i));
  put_record(fbody, 0x03, 0x00, fbytes);
  put_record(fbody, 0x01, 0x00, enc_int(-7));

  auto bytes = build_container(1, 2, fbody.bytes());
  Container c = parse(bytes.data(), bytes.size());
  BW_CHECK_EQ(c.sections[0].records.size(), static_cast<std::size_t>(2));
  BW_CHECK(c.sections[0].records[0].f64 == 1.5);
  BW_CHECK_EQ(c.sections[0].records[1].i64, static_cast<std::int64_t>(-7));
}

BW_TEST(bad_crc_is_lenient_but_strict_throws) {
  ByteWriter body_w;
  put_record(body_w, 0x02, 0x00, enc_uint(99));
  auto bytes = build_container(1, 1, body_w.bytes(), /*flags*/ 0,
                               /*break_body_crc*/ true);

  // Default (lenient): parses fine, flags checksum_ok == false.
  Container c = parse(bytes.data(), bytes.size());
  BW_CHECK(!c.checksum_ok);
  BW_CHECK(!c.sections[0].body_checksum_ok);
  BW_CHECK_EQ(c.sections[0].records[0].u64, static_cast<std::uint64_t>(99));

  // Strict: same input throws kBadChecksum.
  Options strict;
  strict.strict = true;
  bool threw = false;
  try {
    parse(bytes.data(), bytes.size(), strict);
  } catch (const ParseError& e) {
    threw = true;
    BW_CHECK_EQ(e.code(), ErrorCode::kBadChecksum);
  }
  BW_CHECK(threw);
}

BW_TEST(trailer_crc_validates) {
  ByteWriter body_w;
  put_record(body_w, 0x04, 0x00, enc_string("trailer-test"));

  // Build with flags bit1 (has_trailer_crc) and append a correct trailer CRC.
  auto bytes = build_container(1, 1, body_w.bytes(), /*flags*/ 0x02);
  std::uint32_t trailer = bw::common::crc32(bytes.data(), bytes.size());
  for (int i = 0; i < 4; ++i)
    bytes.push_back(static_cast<std::uint8_t>(trailer >> (8 * i)));

  Container c = parse(bytes.data(), bytes.size());
  BW_CHECK(c.has_trailer_crc);
  BW_CHECK(c.trailer_checksum_ok);
  BW_CHECK(c.checksum_ok);
}

BW_TEST(truncated_input_throws_short_read) {
  ByteWriter body_w;
  put_record(body_w, 0x02, 0x00, enc_uint(1));
  auto bytes = build_container(1, 1, body_w.bytes());
  // Chop off the last few body bytes so a declared length runs past EOF.
  bytes.resize(bytes.size() - 2);

  bool threw = false;
  try {
    parse(bytes.data(), bytes.size());
  } catch (const ParseError& e) {
    threw = true;
    // Either the section-range check (kBadLength) or a read past end
    // (kShortRead) is acceptable for a truncated tail; both are hard errors.
    BW_CHECK(e.code() == ErrorCode::kShortRead ||
             e.code() == ErrorCode::kBadLength);
  }
  BW_CHECK(threw);
}

BW_TEST(unknown_tag_throws_bad_type) {
  ByteWriter body_w;
  // tag 0x42 is not a defined record tag.
  put_record(body_w, 0x42, 0x00, enc_uint(0));
  auto bytes = build_container(1, 1, body_w.bytes());

  bool threw = false;
  try {
    parse(bytes.data(), bytes.size());
  } catch (const ParseError& e) {
    threw = true;
    BW_CHECK_EQ(e.code(), ErrorCode::kBadType);
  }
  BW_CHECK(threw);
}

BW_TEST(bad_magic_throws) {
  std::vector<std::uint8_t> junk = {'X', 'X', 'X', 'X', 1, 0, 0, 0};
  bool threw = false;
  try {
    parse(junk.data(), junk.size());
  } catch (const ParseError& e) {
    threw = true;
    BW_CHECK_EQ(e.code(), ErrorCode::kBadMagic);
  }
  BW_CHECK(threw);
}

BW_TEST(depth_limit_triggers) {
  // Build deeply-nested GROUPs from the inside out: innermost is an empty group,
  // each level wraps the previous as its sole record. We exceed max_depth.
  const int levels = 40;  // > default max_depth (32)
  std::vector<std::uint8_t> inner;  // empty innermost group body
  for (int i = 0; i < levels; ++i) {
    ByteWriter w;
    put_record(w, 0x07, 0x01, inner);  // GROUP whose payload is the prior group
    inner = w.bytes();
  }
  auto bytes = build_container(1, 1, inner);

  bool threw = false;
  try {
    parse(bytes.data(), bytes.size());
  } catch (const ParseError& e) {
    threw = true;
    BW_CHECK_EQ(e.code(), ErrorCode::kDepthExceeded);
  }
  BW_CHECK(threw);
}

}  // namespace

BW_TEST_MAIN()
