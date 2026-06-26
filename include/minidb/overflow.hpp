// bytewright - minidb overflow-chain reconstruction.
//
// A record too large for one page is split across a chain of pages linked by the
// page header's `overflow_next` field (0xFFFF terminates the chain). This module
// walks that chain starting from a given page and concatenates each page's
// payload region - the bytes after the 8-byte page header, up to the page's
// `free_start` watermark - into one contiguous buffer.
//
// The walk is bounded on three axes so a corrupt file can never loop or balloon:
//   - a visited-set rejects a cycle (overflow_next pointing back into the chain),
//   - a hard cap on the number of pages followed,
//   - a hard cap on the total reconstructed byte length.
// Rather than throw, the walk returns a structured OverflowResult whose `status`
// records exactly why it stopped, so a diagnostic tool can report "cycle at page
// N" instead of catching an exception.
#ifndef BYTEWRIGHT_MINIDB_OVERFLOW_HPP
#define BYTEWRIGHT_MINIDB_OVERFLOW_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "minidb/decoder.hpp"
#include "minidb/model.hpp"

namespace bw {
namespace minidb {

// Why an overflow walk stopped.
enum class OverflowStatus {
  kComplete,       // reached a page whose overflow_next == kNoOverflow
  kCycle,          // overflow_next pointed at an already-visited page
  kPageLimit,      // followed more pages than the configured maximum
  kLengthLimit,    // reconstructed bytes hit the configured maximum
  kDanglingPage,   // overflow_next pointed outside the decoded page array
  kBadStart,       // the starting page index was itself out of range
};

const char* overflow_status_name(OverflowStatus s) noexcept;

// The outcome of reconstructing a chain.
struct OverflowResult {
  OverflowStatus status = OverflowStatus::kComplete;
  std::vector<std::uint8_t> bytes;     // concatenated payloads, in chain order
  std::vector<std::uint32_t> visited;  // page indices walked, in order
};

// Tunable guards. Defaults are generous but finite.
struct OverflowLimits {
  std::size_t max_pages = 1024;            // chain length cap
  std::size_t max_bytes = 16 * 1024 * 1024;  // total payload cap (16 MiB)
};

// Reconstruct the bytes of the overflow chain starting at `start_page`, reading
// each page's payload from the original buffer via `spans` (parallel to db.pages,
// as produced by parse_pages). The chain is followed through `overflow_next`.
//
// The payload region of a page is [page_header .. free_start), re-clamped to the
// bytes actually available for the page; if free_start is absent/zero or smaller
// than the header, that page contributes nothing (but the walk continues).
OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const std::vector<PageSpan>& spans,
                                    const Database& db, std::uint32_t start_page,
                                    const OverflowLimits& limits);
OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const std::vector<PageSpan>& spans,
                                    const Database& db, std::uint32_t start_page);

// Convenience overload that re-derives the page spans from the decoded Database's
// fixed geometry (page i at kHeaderSize + i*page_size, clamped to EOF) so callers
// holding only a Database need not thread parse_pages' spans through. Equivalent
// to the span-taking overload with reconstructed spans.
OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const Database& db, std::uint32_t start_page,
                                    const OverflowLimits& limits);
OverflowResult reconstruct_overflow(const std::uint8_t* data, std::size_t size,
                                    const Database& db, std::uint32_t start_page);

// Re-derive the page spans for a decoded Database from its fixed page geometry.
// Parallel to db.pages: span i covers page i's bytes (clamped to EOF). Useful for
// modules that hold a Database but not the spans parse_pages produced.
std::vector<PageSpan> derive_spans(std::size_t size, const Database& db);

}  // namespace minidb
}  // namespace bw

#endif  // BYTEWRIGHT_MINIDB_OVERFLOW_HPP
