// bytewright - minidb public facade
//
// minidb decodes the "MDB1" embedded record store and replays its journal/WAL
// into a final committed record set. This header is the entire public surface
// the rest of the monorepo (notably tools/bwdump) depends on: open a buffer into
// a Database model, or render a Database as text.
//
// Errors: open() throws bw::common::ParseError on hard, unrecoverable problems
// (bad magic, bad version, an inconsistent page_size, unknown field/journal
// tags, truncation of a structure it had already committed to reading). A header
// CRC mismatch is NOT a hard error in the default mode: open() records
// `checksum_ok = false` on the model and keeps going, so a fuzzer feeding
// near-valid bytes still reaches the deep page/slot/field/journal paths.
#ifndef BYTEWRIGHT_MINIDB_MINIDB_HPP
#define BYTEWRIGHT_MINIDB_MINIDB_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "minidb/model.hpp"

namespace bw {
namespace minidb {

// Decode options. The default (strict == false) is the fuzzer-reachable mode:
// the header checksum is computed and compared but never aborts the parse.
// Strict mode is used by a unit test to confirm a CRC mismatch is detectable as
// a hard error.
struct Options {
  bool strict = false;  // throw kBadChecksum on a header CRC mismatch
};

// Parse + replay. Throws bw::common::ParseError on hard errors.
Database open(const std::uint8_t* data, std::size_t size, const Options& opts);
Database open(const std::uint8_t* data, std::size_t size);

// Human-readable, multi-line dump of a decoded + replayed Database.
std::string summarize(const Database& db);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_MINIDB_HPP
