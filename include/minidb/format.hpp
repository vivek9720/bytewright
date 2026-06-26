// bytewright - minidb on-wire format constants and the file header struct.
//
// Centralising the byte layout here keeps the decoder (src/minidb/*.cpp) and the
// corpus tooling honest about a single source of truth. Everything is
// little-endian. Offsets below are documented relative to the start of their
// containing structure.
#ifndef BYTEWRIGHT_MINIDB_FORMAT_HPP
#define BYTEWRIGHT_MINIDB_FORMAT_HPP

#include <cstddef>
#include <cstdint>

namespace bw {
namespace minidb {

// 4-byte container magic. The single hard gate (with version) past which we keep
// decoding leniently.
constexpr std::uint8_t kMagic[4] = {'M', 'D', 'B', '1'};

constexpr std::uint16_t kSupportedVersion = 1;

// File header layout (fixed 32 bytes):
//   off 0   magic[4]            "MDB1"
//   off 4   version            u16
//   off 6   page_size          u16   in [64, 8192], multiple of 16
//   off 8   page_count         u32
//   off 12  root_page          u32
//   off 16  string_table_page  u32   (0xFFFFFFFF = none)
//   off 20  journal_offset     u32   (0 = no journal; else abs file offset)
//   off 24  flags              u32
//   off 28  header_crc32       u32   CRC-32 over bytes [0, 28)
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kHeaderCrcCovered = 28;  // bytes the header CRC spans

// page_size validation bounds.
constexpr std::uint16_t kMinPageSize = 64;
constexpr std::uint16_t kMaxPageSize = 8192;
constexpr std::uint16_t kPageSizeAlign = 16;

// Page header layout (fixed 8 bytes, at absolute offset 32 + i*page_size):
//   off 0  page_type      u8
//   off 1  flags          u8
//   off 2  slot_count     u16
//   off 4  free_start     u16
//   off 6  overflow_next  u16  (0xFFFF = none)
constexpr std::size_t kPageHeaderSize = 8;

// Slot directory entry (4 bytes), repeated slot_count times after the header:
//   off 0  rec_offset  u16  (relative to page start)
//   off 2  rec_length  u16
constexpr std::size_t kSlotEntrySize = 4;

constexpr std::uint16_t kNoOverflow = 0xFFFF;
constexpr std::uint32_t kNoStringTable = 0xFFFFFFFF;

// Decoded header, produced by parse_header(). Kept separate from the public
// Database model so header parsing is unit-testable in isolation.
struct FileHeader {
  std::uint16_t version = 0;
  std::uint16_t page_size = 0;
  std::uint32_t page_count = 0;
  std::uint32_t root_page = 0;
  std::uint32_t string_table_page = kNoStringTable;
  std::uint32_t journal_offset = 0;
  std::uint32_t flags = 0;
  std::uint32_t header_crc32 = 0;
  bool checksum_ok = true;
};

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_FORMAT_HPP
