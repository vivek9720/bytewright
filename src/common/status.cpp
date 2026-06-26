#include "common/status.hpp"

#include <string>

namespace bw {
namespace common {

const char* describe(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:
      return "ok";
    case ErrorCode::kShortRead:
      return "read past end of buffer";
    case ErrorCode::kBadMagic:
      return "magic mismatch";
    case ErrorCode::kBadVersion:
      return "unsupported version";
    case ErrorCode::kBadChecksum:
      return "checksum mismatch";
    case ErrorCode::kBadLength:
      return "inconsistent length or offset";
    case ErrorCode::kBadType:
      return "unknown type tag";
    case ErrorCode::kDepthExceeded:
      return "nesting depth exceeded";
    case ErrorCode::kStateViolation:
      return "operation invalid in current state";
    case ErrorCode::kUnsupported:
      return "unsupported feature";
    case ErrorCode::kTruncated:
      return "structure truncated";
  }
  return "unknown error";
}

std::string ParseError::build_message(ErrorCode code, std::size_t offset,
                                      const std::string& what) {
  std::string msg = "[";
  msg += describe(code);
  msg += " @ ";
  // Render the offset without pulling in <sstream> for such a small need.
  if (offset == 0) {
    msg += '0';
  } else {
    char buf[24];
    int i = 0;
    std::size_t v = offset;
    while (v > 0) {
      buf[i++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    }
    while (i > 0) {
      msg += buf[--i];
    }
  }
  msg += "] ";
  msg += what;
  return msg;
}

}  // namespace common
}  // namespace bw
