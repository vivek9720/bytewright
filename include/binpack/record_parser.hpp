// bytewright - binpack record decoder (internal)
//
// The record grammar is shared by every section body, so it lives behind its
// own small interface. A RecordParser owns the depth budget and decodes one
// record (and, recursively, its children) from a ByteReader that has already
// been bounded to the record's payload.
#ifndef BYTEWRIGHT_BINPACK_RECORD_PARSER_HPP
#define BYTEWRIGHT_BINPACK_RECORD_PARSER_HPP

#include <cstddef>
#include <vector>

#include "binpack/model.hpp"
#include "common/byte_reader.hpp"

namespace bw {
namespace binpack {

// Decodes records out of section bodies. `base_offset` is the absolute file
// offset of the byte the bounded reader starts at; it is added to local reader
// offsets so any ParseError points at the real file position, which matters a
// great deal for crash triage and the fuzzer's reproducers.
class RecordParser {
 public:
  RecordParser(std::size_t max_depth, std::size_t base_offset)
      : max_depth_(max_depth), base_offset_(base_offset) {}

  // Decode records until `r` is exhausted. Used for a section body and for the
  // body of a GROUP record.
  std::vector<Record> parse_stream(common::ByteReader& r, std::size_t depth);

  // Decode exactly one record (header + bounded payload).
  Record parse_record(common::ByteReader& r, std::size_t depth);

 private:
  // Interpret an already-bounded payload reader according to `tag`.
  void parse_payload(Record& rec, common::ByteReader& body, std::size_t depth);

  // Thin recursion wrappers (keep parse_payload readable).
  Record inner_parse_record(common::ByteReader& body, std::size_t depth);
  std::vector<Record> inner_parse_stream(common::ByteReader& body,
                                         std::size_t depth);

  std::size_t abs(std::size_t local) const noexcept {
    return base_offset_ + local;
  }

  std::size_t max_depth_;
  std::size_t base_offset_;
};

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_RECORD_PARSER_HPP
