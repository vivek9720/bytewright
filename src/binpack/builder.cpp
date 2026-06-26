// bytewright - binpack container builder
//
// Serializes a logical container (sections + record streams) into the exact
// BPK1 byte layout that src/binpack/parser.cpp reads. See builder.hpp for the
// public surface; this file owns the wire-encoding details, which intentionally
// mirror parser.cpp / record_parser.cpp byte-for-byte:
//
//   header (12 bytes) + header_crc32(4)
//   section table: section_count * 24 bytes
//   section bodies, each a back-to-back record stream
//   optional trailer crc32(4) over the whole file before the trailer
//
//   record: tag u8, flags u8, length varint, payload[length]
//
// CRCs use common::crc32 just like the parser, so a built buffer verifies clean.
#include "binpack/builder.hpp"

#include <cstring>

#include "common/byte_writer.hpp"
#include "common/checksum.hpp"

namespace bw {
namespace binpack {

using common::ByteWriter;

namespace {

constexpr std::size_t kHeaderBytes = 12;       // covered by header_crc
constexpr std::size_t kSectionEntryBytes = 24;

// Map a RecordTag back to its on-wire tag byte. The enum values equal the wire
// bytes (see model.hpp), but going through this switch keeps the dependency
// explicit and survives any future enum reshuffle.
std::uint8_t tag_byte(RecordTag tag) {
  switch (tag) {
    case RecordTag::kInt:    return 0x01;
    case RecordTag::kUint:   return 0x02;
    case RecordTag::kFloat:  return 0x03;
    case RecordTag::kString: return 0x04;
    case RecordTag::kBlob:   return 0x05;
    case RecordTag::kArray:  return 0x06;
    case RecordTag::kGroup:  return 0x07;
    case RecordTag::kKeyval: return 0x08;
  }
  return 0x00;  // unreachable for a well-formed RecordSpec
}

// Encode a record's payload into `w` (no tag/flags/length header). This is the
// inverse of RecordParser::parse_payload.
void encode_payload_into(ByteWriter& w, const RecordSpec& rec) {
  switch (rec.tag) {
    case RecordTag::kInt:
      w.put_svarint(rec.i64);
      break;

    case RecordTag::kUint:
      w.put_varint(rec.u64);
      break;

    case RecordTag::kFloat: {
      // f64 little-endian, bit-for-bit, matching the decoder's memcpy path.
      std::uint64_t bits = 0;
      std::memcpy(&bits, &rec.f64, sizeof(bits));
      for (int i = 0; i < 8; ++i) {
        w.put_u8(static_cast<std::uint8_t>(bits >> (8 * i)));
      }
      break;
    }

    case RecordTag::kString:
      // The whole payload is the string bytes (no length prefix; the record
      // length header bounds it).
      w.put_string(rec.str);
      break;

    case RecordTag::kBlob:
      w.put_bytes(rec.blob);
      break;

    case RecordTag::kArray: {
      // varint element count, then each element as a full record.
      w.put_varint(rec.children.size());
      for (const RecordSpec& child : rec.children) {
        w.put_u8(tag_byte(child.tag));
        w.put_u8(child.group ? 0x01u : 0x00u);
        std::vector<std::uint8_t> payload = encode_record_payload(child);
        w.put_varint(payload.size());
        w.put_bytes(payload);
      }
      break;
    }

    case RecordTag::kGroup:
      // Records back-to-back until the payload is exhausted; no count prefix.
      for (const RecordSpec& child : rec.children) {
        w.put_u8(tag_byte(child.tag));
        w.put_u8(child.group ? 0x01u : 0x00u);
        std::vector<std::uint8_t> payload = encode_record_payload(child);
        w.put_varint(payload.size());
        w.put_bytes(payload);
      }
      break;

    case RecordTag::kKeyval: {
      // key_len varint + key bytes + exactly one nested value record. A
      // well-formed RecordSpec built via RecordSpec::Keyval always has one child;
      // if it somehow has none we still emit a valid (empty-value) shape would be
      // wrong, so we require the child and emit the first one only.
      w.put_varint(rec.key.size());
      w.put_string(rec.key);
      if (!rec.children.empty()) {
        const RecordSpec& value = rec.children.front();
        w.put_u8(tag_byte(value.tag));
        w.put_u8(value.group ? 0x01u : 0x00u);
        std::vector<std::uint8_t> payload = encode_record_payload(value);
        w.put_varint(payload.size());
        w.put_bytes(payload);
      }
      break;
    }
  }
}

}  // namespace

std::vector<std::uint8_t> encode_record_payload(const RecordSpec& rec) {
  ByteWriter w;
  encode_payload_into(w, rec);
  return w.bytes();
}

std::vector<std::uint8_t> encode_record(const RecordSpec& rec) {
  ByteWriter w;
  w.put_u8(tag_byte(rec.tag));
  w.put_u8(rec.group ? 0x01u : 0x00u);
  std::vector<std::uint8_t> payload = encode_record_payload(rec);
  w.put_varint(payload.size());
  w.put_bytes(payload);
  return w.bytes();
}

SectionSpec& Builder::add_section(SectionType type, std::uint16_t id,
                                  std::uint16_t flags) {
  SectionSpec s;
  s.type = type;
  s.id = id;
  s.flags = flags;
  sections_.push_back(std::move(s));
  item_count_explicit_.push_back(false);
  return sections_.back();
}

Builder& Builder::add_record(RecordSpec rec) {
  // Appending a record to a non-existent section is a programming error; we
  // tolerate it by silently no-op'ing rather than throwing, since the builder is
  // not a parser and "invalid" here means misuse, not bad input.
  if (!sections_.empty()) {
    sections_.back().records.push_back(std::move(rec));
  }
  return *this;
}

Builder& Builder::set_item_count(std::uint32_t n) {
  if (!sections_.empty()) {
    sections_.back().item_count = n;
    item_count_explicit_.back() = true;
  }
  return *this;
}

std::vector<std::uint8_t> Builder::build() const {
  ByteWriter w;

  // --- File header (12 bytes covered by header_crc) ----------------------
  std::uint16_t flags = 0;
  if (has_metadata_) flags |= 0x01u;
  if (has_trailer_crc_) flags |= 0x02u;

  w.put_u8('B'); w.put_u8('P'); w.put_u8('K'); w.put_u8('1');
  w.put_u16le(version_);
  w.put_u16le(flags);
  w.put_u16le(static_cast<std::uint16_t>(sections_.size()));
  w.put_u16le(0);  // reserved

  const std::uint32_t header_crc = common::crc32(w.bytes().data(), kHeaderBytes);
  w.put_u32le(header_crc);

  // --- Section table -----------------------------------------------------
  // Encode each section body up front so we know its length and CRC, then lay
  // out the table with reserved offset slots that get patched once we know where
  // each body lands. This two-pass shape mirrors how a real writer streams a
  // table-of-contents ahead of the payloads.
  std::vector<std::vector<std::uint8_t>> bodies;
  bodies.reserve(sections_.size());
  for (const SectionSpec& s : sections_) {
    ByteWriter body;
    // FREE sections carry no decoded body; emit them with an empty body so the
    // table entry is consistent and parse() skips decoding (matching parser.cpp).
    if (s.type != SectionType::kFree) {
      for (const RecordSpec& rec : s.records) {
        body.put_u8(tag_byte(rec.tag));
        body.put_u8(rec.group ? 0x01u : 0x00u);
        std::vector<std::uint8_t> payload = encode_record_payload(rec);
        body.put_varint(payload.size());
        body.put_bytes(payload);
      }
    }
    bodies.push_back(body.bytes());
  }

  std::vector<std::size_t> offset_slots;
  offset_slots.reserve(sections_.size());
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const SectionSpec& s = sections_[i];
    const std::vector<std::uint8_t>& body = bodies[i];

    w.put_u16le(static_cast<std::uint16_t>(s.type));
    w.put_u16le(s.id);
    w.put_u16le(s.flags);
    w.put_u16le(0);  // reserved

    offset_slots.push_back(w.reserve_u32le());  // offset, patched below
    w.put_u32le(static_cast<std::uint32_t>(body.size()));  // length

    // item_count: explicit value if set, else the top-level record count.
    std::uint32_t item_count = s.item_count;
    if (!item_count_explicit_[i]) {
      item_count = static_cast<std::uint32_t>(s.records.size());
    }
    w.put_u32le(item_count);

    const std::uint32_t body_crc = common::crc32(body.data(), body.size());
    w.put_u32le(body_crc);
  }

  // --- Section bodies ----------------------------------------------------
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const std::uint32_t body_offset = static_cast<std::uint32_t>(w.size());
    w.patch_u32le(offset_slots[i], body_offset);
    w.put_bytes(bodies[i]);
  }

  // --- Trailer -----------------------------------------------------------
  if (has_trailer_crc_) {
    // CRC over the whole file produced so far (everything before the trailer).
    const std::uint32_t trailer_crc =
        common::crc32(w.bytes().data(), w.size());
    w.put_u32le(trailer_crc);
  }

  return w.bytes();
}

}  // namespace binpack
}  // namespace bw
