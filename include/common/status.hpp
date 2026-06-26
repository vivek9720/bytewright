// bytewright - common error handling primitives
//
// All decoders in this monorepo share a single structured error model. A
// recoverable decode failure is reported by throwing a ParseError that carries
// a machine-readable code plus the byte offset at which the problem was seen.
// Top-level entry points (and the fuzz harnesses) catch ParseError so that a
// malformed input is treated as "rejected", never as a process abort.
#ifndef BYTEWRIGHT_COMMON_STATUS_HPP
#define BYTEWRIGHT_COMMON_STATUS_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace bw {
namespace common {

// Stable error taxonomy shared by every subproject. The numeric values are not
// part of any wire format; they exist so callers can branch on a cause without
// string matching.
enum class ErrorCode : std::uint16_t {
  kOk = 0,
  kShortRead,        // ran past the end of the input buffer
  kBadMagic,         // container/frame magic did not match
  kBadVersion,       // version field outside the supported range
  kBadChecksum,      // stored checksum disagreed with the computed one
  kBadLength,        // a length/offset field was internally inconsistent
  kBadType,          // an unknown or out-of-range type tag was seen
  kDepthExceeded,    // nested structure recursed beyond the configured limit
  kStateViolation,   // an operation was invalid for the current decoder state
  kUnsupported,      // a well-formed but unimplemented feature was requested
  kTruncated,        // a multi-part structure ended before it was complete
};

const char* describe(ErrorCode code) noexcept;

// Thrown on any recoverable decode failure. `offset` is the absolute position
// in the originating buffer, which makes crash triage and test assertions far
// easier than a bare message.
class ParseError : public std::runtime_error {
 public:
  ParseError(ErrorCode code, std::size_t offset, const std::string& what)
      : std::runtime_error(build_message(code, offset, what)),
        code_(code),
        offset_(offset) {}

  ParseError(ErrorCode code, std::size_t offset)
      : ParseError(code, offset, describe(code)) {}

  ErrorCode code() const noexcept { return code_; }
  std::size_t offset() const noexcept { return offset_; }

 private:
  static std::string build_message(ErrorCode code, std::size_t offset,
                                   const std::string& what);

  ErrorCode code_;
  std::size_t offset_;
};

}  // namespace common
}  // namespace bw

#endif  // BYTEWRIGHT_COMMON_STATUS_HPP
