// bytewright - minidb record/field decoding.
//
// A record is `field_count` (varint) followed by that many fields. Each field is
// a 1-byte type tag plus a type-dependent payload (see FieldType). STRING_REF
// fields carry a varint id that we resolve against the decoded string table; an
// id past the end of the table is recorded as unresolved rather than aborting
// the parse (a real store can outlive a since-rewritten string page).
#include "minidb/decoder.hpp"

#include <cstring>

#include "common/byte_reader.hpp"
#include "common/status.hpp"

namespace bw {
namespace minidb {

using common::ErrorCode;
using common::ParseError;

namespace {

// Read a length-prefixed byte run (varint len + bytes) via the bounded reader.
std::vector<std::uint8_t> read_lenprefixed(common::ByteReader& r) {
  const std::uint64_t len = r.read_varint();
  // require() guards against a length that overruns the buffer; read_bytes also
  // re-checks, so there is no path to an out-of-bounds copy here.
  r.require(static_cast<std::size_t>(len));
  return r.read_bytes(static_cast<std::size_t>(len));
}

}  // namespace

Record decode_record(const std::uint8_t* rec, std::size_t rec_len,
                     const std::vector<std::string>& string_table) {
  Record out;
  common::ByteReader r(rec, rec_len);

  const std::uint64_t field_count = r.read_varint();
  out.fields.reserve(field_count < 64 ? static_cast<std::size_t>(field_count)
                                      : static_cast<std::size_t>(64));

  for (std::uint64_t fi = 0; fi < field_count; ++fi) {
    const std::size_t field_off = r.offset();
    const std::uint8_t tag = r.read_u8();
    Field field;
    field.type = static_cast<FieldType>(tag);

    switch (field.type) {
      case FieldType::kNull:
        break;
      case FieldType::kBool:
        field.b = (r.read_u8() != 0);
        break;
      case FieldType::kInt:
        field.i64 = r.read_svarint();
        break;
      case FieldType::kUint:
        field.u64 = r.read_varint();
        break;
      case FieldType::kFloat: {
        // f64 stored as 8 raw LE bytes; reinterpret the bit pattern.
        const std::uint64_t bits = r.read_u64le();
        double d;
        static_assert(sizeof(d) == sizeof(bits), "f64 must be 8 bytes");
        std::memcpy(&d, &bits, sizeof(d));
        field.f64 = d;
        break;
      }
      case FieldType::kStringRef: {
        field.ref_id = r.read_varint();
        if (field.ref_id < string_table.size()) {
          field.str = string_table[static_cast<std::size_t>(field.ref_id)];
          field.ref_resolved = true;
        } else {
          // Unresolved id: keep the raw id, leave str empty, flag it. Realistic
          // and non-fatal - the field is still part of the record.
          field.ref_resolved = false;
        }
        break;
      }
      case FieldType::kInlineString: {
        std::vector<std::uint8_t> bytes = read_lenprefixed(r);
        field.str.assign(bytes.begin(), bytes.end());
        break;
      }
      case FieldType::kBlob:
        field.blob = read_lenprefixed(r);
        break;
      default:
        // Unknown tag is a hard error for the format: we cannot know the payload
        // width, so we cannot resynchronise.
        throw ParseError(ErrorCode::kBadType, field_off,
                         "unknown minidb field type");
    }

    out.fields.push_back(std::move(field));
  }

  return out;
}

}  // namespace minidb
}  // namespace bw
