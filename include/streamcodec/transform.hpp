// bytewright - streamcodec run-length transform
//
// A small, self-contained run-length codec. It is the in-repo transform a
// COMPRESSED message *could* be decoded with; the decode path itself still
// treats compression as metadata and never invokes this. Kept as a pure,
// dependency-free utility so tools and tests can compress/expand byte buffers
// deterministically.
//
// Wire format (a flat sequence of tokens, each a control byte + data):
//   control byte C:
//     bit7 = 1 : RUN  - the next single data byte repeats (C & 0x7F)+1 times,
//                       i.e. 1..128 copies of one byte.
//     bit7 = 0 : LITERAL - the next (C & 0x7F)+1 data bytes are copied
//                          verbatim, i.e. 1..128 literal bytes.
// A run is only worth emitting for >= 2 repeats; the compressor coalesces
// shorter stretches into literals. Decompression is strictly bounded: it never
// reads past the input and refuses to produce more than a caller-supplied cap,
// so a malicious or corrupt token stream can neither read OOB nor trigger a
// runaway allocation.
#ifndef BYTEWRIGHT_STREAMCODEC_TRANSFORM_HPP
#define BYTEWRIGHT_STREAMCODEC_TRANSFORM_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bw {
namespace streamcodec {

// Structured outcome of a decompress attempt. Distinguishing the two failure
// modes lets callers tell "the data is malformed" from "the data is fine but
// too large for my budget".
enum class RleStatus {
  kOk = 0,
  kMalformed,    // a control byte announced data that runs past the input
  kCapExceeded,  // the decoded length would exceed the caller's cap
};

// The maximum bytes one control token can describe (1..128).
constexpr std::size_t kRleMaxTokenLen = 128;

// Compress `input` with the format above. Never fails: every input maps to a
// valid token stream. The result is at most input.size() + ceil(n/128) bytes.
std::vector<std::uint8_t> rle_compress(const std::vector<std::uint8_t>& input);

// Decompress `input`, appending the decoded bytes to `out`. `max_output` caps
// the total decoded size; decoding stops and returns kCapExceeded the moment
// the cap would be passed, leaving `out` holding the bytes decoded so far.
// Returns kMalformed (and leaves `out` partial) if a token's data runs off the
// end of the input. On kOk, `out` holds the full round-tripped buffer.
RleStatus rle_decompress(const std::uint8_t* data, std::size_t size,
                         std::size_t max_output, std::vector<std::uint8_t>& out);

// Vector-input convenience wrapper.
RleStatus rle_decompress(const std::vector<std::uint8_t>& input,
                         std::size_t max_output,
                         std::vector<std::uint8_t>& out);

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_TRANSFORM_HPP
