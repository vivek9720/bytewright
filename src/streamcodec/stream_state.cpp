// bytewright - streamcodec per-stream rolling state implementation.
#include "streamcodec/stream_state.hpp"

namespace bw {
namespace streamcodec {

StreamTable::Rolling& StreamTable::slot(std::uint16_t stream_id) {
  Rolling& s = streams_[stream_id];
  s.stats.stream_id = stream_id;
  return s;
}

void StreamTable::observe(std::uint16_t stream_id, std::uint16_t seq) {
  Rolling& s = slot(stream_id);
  ++s.stats.frames;

  if (!s.seeded) {
    // First frame on this stream just seeds the rolling counter.
    s.last_seq = seq;
    s.seeded = true;
    return;
  }

  if (seq == s.last_seq) {
    // A repeat of the last seq is a duplicate; leave last_seq unchanged.
    ++s.stats.duplicates;
    return;
  }

  const std::uint16_t expected = static_cast<std::uint16_t>(s.last_seq + 1);
  if (seq != expected) {
    // Any forward (or wrapped) jump that is not exactly +1 is treated as a gap:
    // one or more frames are presumed lost between last_seq and seq.
    ++s.stats.gaps;
  }
  s.last_seq = seq;
}

void StreamTable::note_reset(std::uint16_t stream_id) {
  Rolling& s = slot(stream_id);
  ++s.stats.resets;
  // Clear the rolling counter so the next frame on this stream re-seeds it.
  s.seeded = false;
  s.last_seq = 0;
}

std::vector<StreamStats> StreamTable::snapshot() const {
  std::vector<StreamStats> out;
  out.reserve(streams_.size());
  for (const auto& kv : streams_) {
    out.push_back(kv.second.stats);
  }
  return out;
}

}  // namespace streamcodec
}  // namespace bw
