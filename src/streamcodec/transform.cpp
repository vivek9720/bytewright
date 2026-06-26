// bytewright - streamcodec run-length transform implementation.
//
// The compressor scans the input and emits two kinds of token:
//   * a RUN token whenever it sees >= 2 equal bytes in a row (capped at 128
//     repeats per token), and
//   * a LITERAL token for any other stretch of non-repeating bytes (capped at
//     128 literal bytes per token).
// The decompressor is the strict inverse: it validates every token against the
// remaining input length and against the output cap before producing bytes.
#include "streamcodec/transform.hpp"

namespace bw {
namespace streamcodec {

namespace {

// Length of the equal-byte run starting at `i`, capped at kRleMaxTokenLen.
std::size_t run_length(const std::vector<std::uint8_t>& in, std::size_t i) {
  const std::uint8_t v = in[i];
  std::size_t n = 1;
  while (i + n < in.size() && in[i + n] == v && n < kRleMaxTokenLen) {
    ++n;
  }
  return n;
}

// Emit a literal token covering in[start, start+len) (len in 1..128).
void emit_literal(std::vector<std::uint8_t>& out,
                  const std::vector<std::uint8_t>& in, std::size_t start,
                  std::size_t len) {
  out.push_back(static_cast<std::uint8_t>(len - 1));  // bit7 clear => literal
  for (std::size_t k = 0; k < len; ++k) {
    out.push_back(in[start + k]);
  }
}

// Emit a run token: `count` copies of `value` (count in 1..128).
void emit_run(std::vector<std::uint8_t>& out, std::uint8_t value,
              std::size_t count) {
  out.push_back(static_cast<std::uint8_t>(0x80 | (count - 1)));
  out.push_back(value);
}

}  // namespace

std::vector<std::uint8_t> rle_compress(const std::vector<std::uint8_t>& input) {
  std::vector<std::uint8_t> out;
  out.reserve(input.size());  // worst case is bounded close to the input size

  std::size_t i = 0;
  std::size_t lit_start = 0;  // start of the literal run we are accumulating
  std::size_t lit_len = 0;    // its current length (flushed at <=128)

  auto flush_literal = [&]() {
    while (lit_len > 0) {
      const std::size_t take =
          lit_len < kRleMaxTokenLen ? lit_len : kRleMaxTokenLen;
      emit_literal(out, input, lit_start, take);
      lit_start += take;
      lit_len -= take;
    }
  };

  while (i < input.size()) {
    const std::size_t run = run_length(input, i);
    if (run >= 2) {
      // A worthwhile run breaks the pending literal stretch first.
      flush_literal();
      emit_run(out, input[i], run);
      i += run;
      lit_start = i;
      lit_len = 0;
    } else {
      // Single non-repeating byte: extend the literal run, flushing at the cap.
      if (lit_len == kRleMaxTokenLen) {
        flush_literal();
        lit_start = i;
      }
      ++lit_len;
      ++i;
    }
  }
  flush_literal();
  return out;
}

RleStatus rle_decompress(const std::uint8_t* data, std::size_t size,
                         std::size_t max_output,
                         std::vector<std::uint8_t>& out) {
  std::size_t pos = 0;
  while (pos < size) {
    const std::uint8_t control = data[pos++];
    const std::size_t count = static_cast<std::size_t>(control & 0x7F) + 1;

    if ((control & 0x80) != 0) {
      // RUN: exactly one data byte follows.
      if (pos >= size) {
        return RleStatus::kMalformed;  // missing the run's value byte
      }
      if (out.size() + count > max_output) {
        return RleStatus::kCapExceeded;
      }
      const std::uint8_t value = data[pos++];
      out.insert(out.end(), count, value);
    } else {
      // LITERAL: `count` data bytes follow.
      if (pos + count > size) {
        return RleStatus::kMalformed;  // literal runs past the input
      }
      if (out.size() + count > max_output) {
        return RleStatus::kCapExceeded;
      }
      out.insert(out.end(), data + pos, data + pos + count);
      pos += count;
    }
  }
  return RleStatus::kOk;
}

RleStatus rle_decompress(const std::vector<std::uint8_t>& input,
                         std::size_t max_output,
                         std::vector<std::uint8_t>& out) {
  return rle_decompress(input.data(), input.size(), max_output, out);
}

}  // namespace streamcodec
}  // namespace bw
