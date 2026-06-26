// bytewright - unit tests for the shared common utilities.
#include <cstdint>
#include <string>
#include <vector>

#include "bw_test.hpp"
#include "common/bit_reader.hpp"
#include "common/byte_reader.hpp"
#include "common/byte_writer.hpp"
#include "common/checksum.hpp"
#include "common/hashing.hpp"
#include "common/hexdump.hpp"
#include "common/status.hpp"
#include "common/utf8.hpp"

namespace {

using bw::common::BitReader;
using bw::common::ByteReader;
using bw::common::ByteWriter;
using bw::common::ParseError;

std::vector<std::uint8_t> vec(std::initializer_list<std::uint8_t> in) {
  return std::vector<std::uint8_t>(in);
}

}  // namespace

BW_TEST(byte_reader_roundtrips_fixed_and_varint) {
  ByteWriter w;
  w.put_u16le(0x1234);
  w.put_u32be(0xDEADBEEF);
  w.put_varint(300);
  w.put_svarint(-5);
  w.put_u64le(0x0102030405060708ull);

  ByteReader r(w.bytes().data(), w.bytes().size());
  BW_CHECK_EQ(r.read_u16le(), 0x1234u);
  BW_CHECK_EQ(r.read_u32be(), 0xDEADBEEFu);
  BW_CHECK_EQ(r.read_varint(), 300u);
  BW_CHECK_EQ(r.read_svarint(), -5);
  BW_CHECK_EQ(r.read_u64le(), 0x0102030405060708ull);
  BW_CHECK(r.eof());
}

BW_TEST(byte_reader_short_read_throws) {
  auto data = vec({0x01, 0x02});
  ByteReader r(data.data(), data.size());
  BW_CHECK_THROWS(r.read_u32le());
}

BW_TEST(varint_overlong_rejected) {
  // Eleven 0x80 bytes never terminate -> too long.
  std::vector<std::uint8_t> data(11, 0x80);
  ByteReader r(data.data(), data.size());
  BW_CHECK_THROWS(r.read_varint());
}

BW_TEST(bit_reader_msb_first) {
  // 0b1011_0010, 0b1100_0000
  auto data = vec({0xB2, 0xC0});
  BitReader br(data.data(), data.size());
  BW_CHECK_EQ(br.read_bits(1), 1u);   // 1
  BW_CHECK_EQ(br.read_bits(3), 0x3u);  // 011
  BW_CHECK_EQ(br.read_bits(4), 0x2u);  // 0010
  BW_CHECK_EQ(br.read_bits(2), 0x3u);  // 11
  BW_CHECK_EQ(br.bits_remaining(), 6u);
}

BW_TEST(bit_reader_align_and_byte_fastpath) {
  auto data = vec({0xAB, 0xCD, 0xEF});
  BitReader br(data.data(), data.size());
  BW_CHECK_EQ(br.read_bits(4), 0xAu);
  br.align();  // skip to byte boundary
  BW_CHECK_EQ(br.bit_offset(), 8u);
  BW_CHECK_EQ(br.read_bits(16), 0xCDEFu);
  BW_CHECK(br.eof());
}

BW_TEST(bit_reader_overrun_throws) {
  auto data = vec({0xFF});
  BitReader br(data.data(), data.size());
  BW_CHECK_THROWS(br.read_bits(9));
}

BW_TEST(crc32_known_vector) {
  // CRC-32/ISO-HDLC of "123456789" is 0xCBF43926.
  std::string s = "123456789";
  std::uint32_t c = bw::common::crc32(
      reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
  BW_CHECK_EQ(c, 0xCBF43926u);
}

BW_TEST(adler32_known_vector) {
  // Adler-32 of "Wikipedia" is 0x11E60398.
  std::string s = "Wikipedia";
  std::uint32_t a = bw::common::adler32(
      reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
  BW_CHECK_EQ(a, 0x11E60398u);
}

BW_TEST(fnv1a_known_vector_and_incremental) {
  // FNV-1a-32 of "" is the offset basis; of "a" is 0xE40C292C.
  BW_CHECK_EQ(bw::common::fnv1a_32(std::string("")), 0x811C9DC5u);
  BW_CHECK_EQ(bw::common::fnv1a_32(std::string("a")), 0xE40C292Cu);

  std::string s = "foobar";
  std::uint32_t whole = bw::common::fnv1a_32(s);
  std::uint32_t st = bw::common::fnv1a_32_init();
  st = bw::common::fnv1a_32_update(
      st, reinterpret_cast<const std::uint8_t*>(s.data()), 3);
  st = bw::common::fnv1a_32_update(
      st, reinterpret_cast<const std::uint8_t*>(s.data()) + 3, 3);
  BW_CHECK_EQ(st, whole);
}

BW_TEST(mix64_is_a_bijonline_distinct) {
  // Distinct inputs must not trivially collide for sequential keys.
  BW_CHECK(bw::common::mix64(0) != bw::common::mix64(1));
  BW_CHECK(bw::common::mix64(1) != bw::common::mix64(2));
  std::uint64_t a = bw::common::hash_combine(0x1234, 0x5678);
  std::uint64_t b = bw::common::hash_combine(0x5678, 0x1234);
  BW_CHECK(a != b);  // order dependent
}

BW_TEST(utf8_validates_good_and_rejects_bad) {
  // "héllo" + a 3-byte and 4-byte scalar.
  std::string good;
  BW_CHECK(bw::common::utf8::encode(0x68, good));      // h
  BW_CHECK(bw::common::utf8::encode(0xE9, good));       // é
  BW_CHECK(bw::common::utf8::encode(0x20AC, good));     // euro sign
  BW_CHECK(bw::common::utf8::encode(0x1F600, good));    // emoji
  BW_CHECK(bw::common::utf8::validate(good));
  std::size_t n = 0;
  BW_CHECK(bw::common::utf8::count_codepoints(
      reinterpret_cast<const std::uint8_t*>(good.data()), good.size(), n));
  BW_CHECK_EQ(n, 4u);

  // Overlong encoding of '/' (0x2F) as C0 AF must be rejected.
  auto overlong = vec({0xC0, 0xAF});
  BW_CHECK(!bw::common::utf8::validate(
      std::string(overlong.begin(), overlong.end())));

  // Lone continuation byte and truncated sequence.
  BW_CHECK(!bw::common::utf8::validate(std::string(1, '\x80')));
  auto truncated = vec({0xE0, 0xA4});  // 3-byte lead, only 2 bytes
  BW_CHECK(!bw::common::utf8::validate(
      std::string(truncated.begin(), truncated.end())));
}

BW_TEST(utf8_surrogates_rejected) {
  std::string out;
  BW_CHECK(!bw::common::utf8::encode(0xD800, out));  // high surrogate
  BW_CHECK(!bw::common::utf8::encode(0x110000, out));  // out of range
  BW_CHECK(out.empty());
}

BW_TEST(hexdump_and_escape_nonempty) {
  auto data = vec({0x00, 0x41, 0x7F, 0xFF});
  std::string dump = bw::common::hexdump(data);
  BW_CHECK(dump.size() > 0);
  std::string esc = bw::common::escape(std::string("a\nb"));
  BW_CHECK(esc == std::string("a\\x0ab"));
  BW_CHECK_EQ(bw::common::to_hex(0xABCD, 4), std::string("0xabcd"));
}

BW_TEST_MAIN()
