// bytewright - binpack record decoder
//
// Record wire layout:
//   tag    u8
//   flags  u8        (bit0 = group/nested marker; other bits reserved)
//   length varint    (payload byte count)
//   payload[length]
//
// The payload is interpreted by `tag`. We always carve a *bounded* sub-reader
// over exactly `length` payload bytes and decode within it; that way a record
// can never read past its own declared payload, and the parent stream stays in
// sync no matter how the child decodes. Container tags (ARRAY/GROUP/KEYVAL)
// recurse, guarded by a depth budget.
#include "binpack/record_parser.hpp"

#include <cstring>

#include "common/status.hpp"

namespace bw {
namespace binpack {

using common::ByteReader;
using common::ErrorCode;
using common::ParseError;

std::vector<Record> RecordParser::parse_stream(ByteReader& r,
                                               std::size_t depth) {
  std::vector<Record> out;
  // A stream is "records back-to-back until the bounded reader is empty". An
  // empty body is legal and yields no records.
  while (!r.eof()) {
    out.push_back(parse_record(r, depth));
  }
  return out;
}

Record RecordParser::parse_record(ByteReader& r, std::size_t depth) {
  if (depth > max_depth_) {
    throw ParseError(ErrorCode::kDepthExceeded, abs(r.offset()),
                     "record nesting exceeded max depth");
  }

  const std::size_t rec_off = r.offset();
  Record rec;

  const std::uint8_t tag_byte = r.read_u8();
  const std::uint8_t flags = r.read_u8();
  rec.group = (flags & 0x01u) != 0;

  switch (tag_byte) {
    case 0x01: rec.tag = RecordTag::kInt;    break;
    case 0x02: rec.tag = RecordTag::kUint;   break;
    case 0x03: rec.tag = RecordTag::kFloat;  break;
    case 0x04: rec.tag = RecordTag::kString; break;
    case 0x05: rec.tag = RecordTag::kBlob;   break;
    case 0x06: rec.tag = RecordTag::kArray;  break;
    case 0x07: rec.tag = RecordTag::kGroup;  break;
    case 0x08: rec.tag = RecordTag::kKeyval; break;
    default:
      throw ParseError(ErrorCode::kBadType, abs(rec_off),
                       "unknown record tag");
  }

  const std::uint64_t length = r.read_varint();

  // Borrow the payload bytes and build a sub-reader bounded to exactly them.
  // read_raw() advances the parent cursor and throws kShortRead if the declared
  // payload runs off the end of the parent buffer, so the parent stays correct.
  const std::size_t payload_off = r.offset();
  const std::uint8_t* payload = r.read_raw(static_cast<std::size_t>(length));
  ByteReader body(payload, static_cast<std::size_t>(length));

  // Translate the bounded reader's offsets back into absolute file offsets.
  RecordParser inner(max_depth_, base_offset_ + payload_off);
  inner.parse_payload(rec, body, depth);

  return rec;
}

void RecordParser::parse_payload(Record& rec, ByteReader& body,
                                 std::size_t depth) {
  switch (rec.tag) {
    case RecordTag::kInt:
      rec.i64 = body.read_svarint();
      break;

    case RecordTag::kUint:
      rec.u64 = body.read_varint();
      break;

    case RecordTag::kFloat: {
      // f64 little-endian, copied bit-for-bit. read_raw bounds-checks the 8.
      const std::uint8_t* p = body.read_raw(sizeof(double));
      std::uint64_t bits = 0;
      for (int i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(p[i]) << (8 * i);
      }
      double d;
      std::memcpy(&d, &bits, sizeof(d));
      rec.f64 = d;
      break;
    }

    case RecordTag::kString:
      // The whole remaining payload is the string. read_string copies it.
      rec.str = body.read_string(body.remaining());
      break;

    case RecordTag::kBlob:
      rec.blob = body.read_bytes(body.remaining());
      break;

    case RecordTag::kArray: {
      // varint element count, then exactly `count` nested records. We trust the
      // count but every child still decodes inside this bounded body, so a lie
      // can only cause a kShortRead, never an over-read.
      const std::uint64_t count = body.read_varint();
      rec.children.reserve(count < 4096 ? static_cast<std::size_t>(count) : 0);
      for (std::uint64_t i = 0; i < count; ++i) {
        rec.children.push_back(inner_parse_record(body, depth + 1));
      }
      break;
    }

    case RecordTag::kGroup:
      // Nested records until this group's payload is exhausted.
      rec.children = inner_parse_stream(body, depth + 1);
      break;

    case RecordTag::kKeyval: {
      // key_len varint + key bytes + exactly one nested record (the value).
      const std::uint64_t key_len = body.read_varint();
      rec.key = body.read_string(static_cast<std::size_t>(key_len));
      rec.children.push_back(inner_parse_record(body, depth + 1));
      // Anything left over after the single value is a malformed keyval body.
      if (!body.eof()) {
        throw ParseError(ErrorCode::kBadLength, abs(body.offset()),
                         "trailing bytes after keyval value");
      }
      break;
    }
  }

  // For scalar tags, tolerate trailing pad bytes silently (a real format often
  // word-aligns payloads); for the container tags above we were strict where it
  // mattered. This keeps the decoder lenient without losing structure.
}

// Small wrappers so the recursive calls above read naturally. `body` is already
// a bounded reader whose absolute base we tracked when constructing this parser.
Record RecordParser::inner_parse_record(ByteReader& body, std::size_t depth) {
  return parse_record(body, depth);
}

std::vector<Record> RecordParser::inner_parse_stream(ByteReader& body,
                                                     std::size_t depth) {
  return parse_stream(body, depth);
}

}  // namespace binpack
}  // namespace bw
