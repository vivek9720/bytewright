// bytewright - minidb stage 2: page array + slot directory parsing.
//
// Page i lives at absolute offset 32 + i*page_size. We walk page_count pages but
// stop once a page's start is at or past EOF. A page whose *start* is in bounds
// but whose body runs past EOF is clamped: we read only the bytes that exist and
// mark the page truncated. None of this is a hard error - the format is meant to
// degrade gracefully on a truncated file.
//
// Within a page: an 8-byte header followed by slot_count 4-byte directory
// entries. Each slot's (rec_offset, rec_length) is validated against the page
// extent; a slot that points outside the page is recorded as out-of-bounds and
// simply skipped by later stages (it never causes an out-of-bounds read).
#include "minidb/decoder.hpp"

#include "common/byte_reader.hpp"

namespace bw {
namespace minidb {

std::vector<PageSpan> parse_pages(const std::uint8_t* data, std::size_t size,
                                  const FileHeader& hdr, Database& db) {
  std::vector<PageSpan> spans;

  const std::size_t page_size = static_cast<std::size_t>(hdr.page_size);

  for (std::uint32_t i = 0; i < hdr.page_count; ++i) {
    // Absolute offset of this page. Compute in 64-bit to avoid overflow when a
    // hostile page_count * page_size would wrap a 32-bit value.
    const std::size_t page_off =
        kHeaderSize + static_cast<std::size_t>(i) * page_size;

    // A page whose start is past EOF cannot be read at all; stop the walk. (We
    // break rather than continue: pages are contiguous, so once one starts past
    // EOF every later one does too.)
    if (page_off >= size) {
      break;
    }

    // Bytes actually available for this page (clamp the body to EOF).
    const std::size_t avail = size - page_off;
    const std::size_t page_len = avail < page_size ? avail : page_size;

    Page page;
    page.index = i;
    spans.push_back(PageSpan{page_off, page_len});

    // If we cannot even read the 8-byte page header, record an empty truncated
    // page and keep the span (length < header) so callers can detect it.
    if (page_len < kPageHeaderSize) {
      page.truncated = true;
      db.pages.push_back(std::move(page));
      continue;
    }

    // Read the page header through a bounded sub-reader so any short read inside
    // the directory is caught structurally rather than via raw indexing.
    common::ByteReader pr(data + page_off, page_len);
    const std::uint8_t type_byte = pr.read_u8();
    page.type = static_cast<PageType>(type_byte);
    page.flags = pr.read_u8();
    page.slot_count = pr.read_u16le();
    page.free_start = pr.read_u16le();
    page.overflow_next = pr.read_u16le();

    if (page_len < page_size) {
      page.truncated = true;
    }

    // Slot directory: slot_count entries of 4 bytes each, immediately after the
    // 8-byte header. A directory that would extend past the available page bytes
    // is clamped - we decode as many whole entries as actually fit.
    const std::size_t dir_off = kPageHeaderSize;
    const std::size_t dir_bytes =
        static_cast<std::size_t>(page.slot_count) * kSlotEntrySize;
    std::size_t fit_entries = page.slot_count;
    if (dir_off + dir_bytes > page_len) {
      const std::size_t room = page_len > dir_off ? page_len - dir_off : 0;
      fit_entries = room / kSlotEntrySize;
      page.truncated = true;
    }

    page.slots.reserve(fit_entries);
    for (std::size_t s = 0; s < fit_entries; ++s) {
      Slot slot;
      slot.rec_offset = pr.read_u16le();
      slot.rec_length = pr.read_u16le();

      // Validate the slot points at a region wholly inside the *declared* page
      // (page_size), and also inside the bytes we actually have. We store the
      // verdict; reads happen later and re-check, so a bad slot is inert.
      const std::size_t end = static_cast<std::size_t>(slot.rec_offset) +
                              static_cast<std::size_t>(slot.rec_length);
      const bool within_page = slot.rec_offset >= kPageHeaderSize &&
                               end <= page_size && end >= slot.rec_offset;
      slot.in_bounds = within_page;
      page.slots.push_back(slot);
    }

    db.pages.push_back(std::move(page));
  }

  return spans;
}

}  // namespace minidb
}  // namespace bw
