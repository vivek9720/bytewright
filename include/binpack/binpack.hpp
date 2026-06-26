// bytewright - binpack public facade
//
// binpack decodes the "BPK1" tagged binary container format. This header is the
// entire public surface the rest of the monorepo (notably tools/bwdump) depends
// on: parse a buffer into a Container model, or render a Container as text.
//
// Errors: parse() throws bw::common::ParseError on hard, unrecoverable problems
// (bad magic, bad version, inconsistent lengths/offsets, unknown tags, runaway
// nesting, truncation). Checksum mismatches are NOT hard errors in the default
// mode: parse() records `checksum_ok = false` on the model and keeps going, so a
// fuzzer feeding near-valid bytes still reaches the deep decode paths.
#ifndef BYTEWRIGHT_BINPACK_BINPACK_HPP
#define BYTEWRIGHT_BINPACK_BINPACK_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "binpack/model.hpp"

namespace bw {
namespace binpack {

// Decode options. The default (strict == false) is the fuzzer-reachable mode:
// checksums are computed and compared but never abort the parse. Strict mode is
// used by a unit test to confirm a CRC mismatch is detectable as a hard error.
struct Options {
  bool strict = false;       // throw kBadChecksum on any checksum mismatch
  std::size_t max_depth = 32;  // record nesting limit -> kDepthExceeded
};

Container parse(const std::uint8_t* data, std::size_t size, const Options& opts);
Container parse(const std::uint8_t* data, std::size_t size);

std::string summarize(const Container& c);

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_BINPACK_HPP
