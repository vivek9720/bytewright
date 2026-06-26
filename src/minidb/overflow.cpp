// bytewright - minidb overflow-chain reconstruction (impl).
//
// See overflow.hpp for the contract. The walk maintains a visited-set of page
// indices to detect cycles, and bails the moment a guard (page count, total
// length, dangling/out-of-range link) trips - always returning a structured
// status rather than throwing or looping. Each page's payload is the byte range
// [kPageHeaderSize, free_start) clamped to the bytes actually available, copied
// straight out of the original buffer through its PageSpan.
#include "minidb/overflow.hpp"

#include <unordered_set>

#include "minidb/format.hpp"

namespace bw {
namespace minidb {

const char* overflow_status_name(OverflowStatus s) noexcept {
  switch (s) {
    case OverflowStatus::kComplete:
      return "COMPLETE";
    case OverflowStatus::kCycle:
      return "CYCLE";
    case OverflowStatus::kPageLimit:
      return "PAGE_LIMIT";
    case OverflowStatus::kLengthLimit:
      return "LENGTH_LIMIT";
    case OverflowStatus::kDanglingPage:
      return "DANGLING_PAGE";
    case OverflowStatus::kBadStart:
      return "BAD_START";
  }
  return "UNKNOWN";
}

OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const std::vector<PageSpan>& spans,
                                    const Database& db, std::uint32_t start_page,
                                    const OverflowLimits& limits) {
  OverflowResult result;

  // The start must name a page we actually decoded (and have a span for).
  if (start_page >= db.pages.size() || start_page >= spans.size()) {
    result.status = OverflowStatus::kBadStart;
    return result;
  }

  std::unordered_set<std::uint32_t> seen;
  std::uint32_t current = start_page;

  while (true) {
    // Cycle guard: if we have already walked this page, the chain loops.
    if (seen.find(current) != seen.end()) {
      result.status = OverflowStatus::kCycle;
      return result;
    }
    // Page-count guard: stop before walking more than the configured maximum.
    if (result.visited.size() >= limits.max_pages) {
      result.status = OverflowStatus::kPageLimit;
      return result;
    }

    seen.insert(current);
    result.visited.push_back(current);

    const Page& page = db.pages[current];
    const PageSpan& span = spans[current];

    // Payload region: [kPageHeaderSize, free_start), clamped to the bytes that
    // actually exist for this page. A page with no usable payload contributes
    // nothing but does not stop the walk.
    std::size_t payload_end = static_cast<std::size_t>(page.free_start);
    if (payload_end > span.length) {
      payload_end = span.length;  // clamp to bytes available
    }
    if (payload_end > kPageHeaderSize && span.length > kPageHeaderSize) {
      const std::size_t abs_start = span.file_offset + kPageHeaderSize;
      const std::size_t abs_end = span.file_offset + payload_end;
      // Defensive re-check against the whole buffer (spans were already clamped
      // to EOF by parse_pages, but we never trust a single source).
      if (abs_start < size && abs_end <= size && abs_end >= abs_start) {
        const std::size_t add = abs_end - abs_start;
        // Length guard: refuse to grow past the configured byte cap.
        if (result.bytes.size() + add > limits.max_bytes) {
          result.status = OverflowStatus::kLengthLimit;
          return result;
        }
        result.bytes.insert(result.bytes.end(), data + abs_start,
                            data + abs_end);
      }
    }

    // Follow the link. 0xFFFF terminates the chain cleanly.
    const std::uint16_t nxt = page.overflow_next;
    if (nxt == kNoOverflow) {
      result.status = OverflowStatus::kComplete;
      return result;
    }
    const std::uint32_t next_page = static_cast<std::uint32_t>(nxt);
    if (next_page >= db.pages.size() || next_page >= spans.size()) {
      // The link points beyond the decoded page array: a dangling pointer.
      result.status = OverflowStatus::kDanglingPage;
      return result;
    }
    current = next_page;
  }
}

OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const std::vector<PageSpan>& spans,
                                    const Database& db,
                                    std::uint32_t start_page) {
  return reconstruct_overflow(data, size, spans, db, start_page,
                              OverflowLimits{});
}

std::vector<PageSpan> derive_spans(std::size_t size, const Database& db) {
  std::vector<PageSpan> spans;
  spans.reserve(db.pages.size());
  const std::size_t page_size = static_cast<std::size_t>(db.page_size);
  for (std::size_t i = 0; i < db.pages.size(); ++i) {
    // Mirror parse_pages' geometry: page i begins right after the 32-byte header.
    const std::size_t off = kHeaderSize + i * page_size;
    if (off >= size || page_size == 0) {
      // Past EOF (or a degenerate page_size): an empty span keeps the vector
      // parallel to db.pages without ever yielding readable bytes.
      spans.push_back(PageSpan{off, 0});
      continue;
    }
    const std::size_t avail = size - off;
    const std::size_t len = avail < page_size ? avail : page_size;
    spans.push_back(PageSpan{off, len});
  }
  return spans;
}

OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const Database& db, std::uint32_t start_page,
                                    const OverflowLimits& limits) {
  const std::vector<PageSpan> spans = derive_spans(size, db);
  return reconstruct_overflow(data, size, spans, db, start_page, limits);
}

OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const Database& db,
                                    std::uint32_t start_page) {
  return reconstruct_overflow(data, size, db, start_page, OverflowLimits{});
}

}  // namespace minidb
}  // namespace bw
