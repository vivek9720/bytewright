// bytewright - streamcodec per-stream rolling state
//
// Each stream_id carries a rolling 16-bit frame counter. We observe the `seq`
// of every frame on a stream and classify it relative to the last seen value:
//   * the very first frame just seeds the counter;
//   * seq == last        -> duplicate;
//   * seq == last + 1    -> in order (the common case);
//   * any other forward jump -> a gap (frames presumed lost).
// A RESET clears the rolling state so the next frame re-seeds, and bumps a
// per-stream reset counter so the lifecycle is visible in the stats.
#ifndef BYTEWRIGHT_STREAMCODEC_STREAM_STATE_HPP
#define BYTEWRIGHT_STREAMCODEC_STREAM_STATE_HPP

#include <cstdint>
#include <map>
#include <vector>

namespace bw {
namespace streamcodec {

// Aggregated, emit-ready stats for one stream.
struct StreamStats {
  std::uint16_t stream_id = 0;
  std::uint64_t frames = 0;
  std::uint64_t gaps = 0;
  std::uint64_t duplicates = 0;
  std::uint64_t resets = 0;
};

class StreamTable {
 public:
  // Record a frame's seq on its stream, updating frame/gap/duplicate counters.
  void observe(std::uint16_t stream_id, std::uint16_t seq);

  // Clear the rolling seq for a stream and bump its reset counter.
  void note_reset(std::uint16_t stream_id);

  // Snapshot of every stream's stats, ordered by stream_id (std::map order).
  std::vector<StreamStats> snapshot() const;

 private:
  struct Rolling {
    StreamStats stats;
    std::uint16_t last_seq = 0;
    bool seeded = false;  // false until the first frame seeds last_seq
  };

  Rolling& slot(std::uint16_t stream_id);

  std::map<std::uint16_t, Rolling> streams_;
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_STREAM_STATE_HPP
