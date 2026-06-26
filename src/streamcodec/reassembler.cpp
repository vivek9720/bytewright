// bytewright - streamcodec message reassembler implementation.
//
// The buffer for a message grows on demand as fragments land at their
// frag_offset. Filled byte ranges are tracked as a sorted, coalesced list of
// [begin, end) spans; a message is complete once those spans collapse to the
// single span [0, total_len) and FIN has fixed total_len.
#include "streamcodec/reassembler.hpp"

#include <algorithm>

namespace bw {
namespace streamcodec {

void Reassembler::add_range(PartialMessage& p, std::uint32_t begin,
                            std::uint32_t end) {
  if (end <= begin) {
    return;  // empty fragment contributes no coverage
  }

  // Insert keeping the list sorted by begin, then coalesce neighbours that
  // touch or overlap. The list stays small (one entry per contiguous run), so
  // the linear merge is cheap in practice.
  p.ranges.emplace_back(begin, end);
  std::sort(p.ranges.begin(), p.ranges.end());

  std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
  for (const auto& span : p.ranges) {
    if (!merged.empty() && span.first <= merged.back().second) {
      merged.back().second = std::max(merged.back().second, span.second);
    } else {
      merged.push_back(span);
    }
  }
  p.ranges.swap(merged);
}

bool Reassembler::is_complete(const PartialMessage& p) {
  if (!p.have_fin) {
    return false;
  }
  if (p.total_len == 0) {
    // A zero-length message is complete the moment FIN fixes its length.
    return true;
  }
  // After coalescing, full coverage means exactly one span equal to the whole.
  return p.ranges.size() == 1 && p.ranges.front().first == 0 &&
         p.ranges.front().second >= p.total_len;
}

bool Reassembler::accept_data(const Frame& f, Message& out) {
  const Key key{f.stream_id, f.msg_id};
  PartialMessage& p = partials_[key];

  // The COMPRESSED flag is metadata: we record it on the message but never
  // touch the payload bytes. Any fragment carrying it marks the whole message.
  if (f.compressed()) {
    p.compressed = true;
  }

  // Where do these payload bytes belong? A non-FRAG DATA frame is a single-shot
  // message placed at offset 0; a FRAG frame places its payload at frag_offset.
  const std::uint32_t begin = f.frag() ? f.frag_offset : 0u;

  // Compute the exclusive end with overflow-safe widening, then place the bytes.
  const std::uint64_t end64 =
      static_cast<std::uint64_t>(begin) + f.payload.size();
  const std::uint32_t end =
      static_cast<std::uint32_t>(std::min<std::uint64_t>(end64, 0xFFFFFFFFu));

  // Grow the reassembly buffer to cover [begin, end) and copy the fragment in.
  if (end > p.buffer.size()) {
    p.buffer.resize(end);
  }
  if (!f.payload.empty() && begin < p.buffer.size()) {
    const std::size_t n =
        std::min<std::size_t>(f.payload.size(), p.buffer.size() - begin);
    std::copy_n(f.payload.begin(), n,
                p.buffer.begin() + static_cast<std::ptrdiff_t>(begin));
  }

  add_range(p, begin, end);
  p.high_water = std::max(p.high_water, end);

  // FIN fixes the message's total length to the highest end offset observed.
  if (f.fin()) {
    p.have_fin = true;
    p.total_len = p.high_water;
  }

  if (!is_complete(p)) {
    return false;
  }

  // Emit the contiguous payload and retire the in-flight state.
  out.stream_id = f.stream_id;
  out.msg_id = f.msg_id;
  out.compressed = p.compressed;
  out.data.assign(p.buffer.begin(),
                  p.buffer.begin() + static_cast<std::ptrdiff_t>(p.total_len));
  partials_.erase(key);
  return true;
}

std::size_t Reassembler::reset_stream(std::uint16_t stream_id) {
  // RESET lifecycle: drop every partial buffer for this stream. Partially
  // received messages are discarded and never emitted.
  std::size_t dropped = 0;
  for (auto it = partials_.begin(); it != partials_.end();) {
    if (it->first.first == stream_id) {
      it = partials_.erase(it);
      ++dropped;
    } else {
      ++it;
    }
  }
  return dropped;
}

}  // namespace streamcodec
}  // namespace bw
