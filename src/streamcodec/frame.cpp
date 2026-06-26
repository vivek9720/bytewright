// bytewright - streamcodec frame helpers: type names and the header CRC-16.
#include "streamcodec/frame.hpp"

namespace bw {
namespace streamcodec {

const char* frame_type_name(FrameType type) noexcept {
  switch (type) {
    case FrameType::kData:    return "DATA";
    case FrameType::kControl: return "CONTROL";
    case FrameType::kAck:     return "ACK";
    case FrameType::kReset:   return "RESET";
  }
  return "UNKNOWN";
}

// CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no input/output reflection
// and no final xor. Computed bit-by-bit; the header is short so a table would be
// overkill and the loop keeps the format self-documenting.
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < size; ++i) {
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000u) {
        crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021u);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace streamcodec
}  // namespace bw
