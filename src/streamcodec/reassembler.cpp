// bytewright - streamcodec message reassembler implementation.
//
// The buffer for a message grows on demand as fragments land at their
// frag_offset. Filled byte ranges are tracked as a sorted, coalesced list of
// [begin, end) spans; a message is complete once those spans collapse to the
// single span [0, total_len) and FIN has fixed total_len.
//
// In-flight partials are kept in `store_` (a vector) with `index_` mapping each
// (stream_id, msg_id) key to its slot. Slots are reclaimed two ways: a completed
// message is removed with swap-and-pop, and a RESET compacts away every slot for
// the stream being reset.
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

std::size_t Reassembler::slot_for(const Key& key, std::uint16_t stream_id,
                                  std::uint32_t msg_id) {
  auto it = index_.find(key);
  if (it != index_.end()) {
    return it->second;
  }
  PartialMessage fresh;
  fresh.stream_id = stream_id;
  fresh.msg_id = msg_id;
  store_.push_back(std::move(fresh));
  const std::size_t idx = store_.size() - 1;
  index_[key] = idx;
  return idx;
}

void Reassembler::remove_slot(std::size_t idx, const Key& key) {
  index_.erase(key);
  const std::size_t last = store_.size() - 1;
  if (idx != last) {
    // Move the tail element into the hole and repoint its index entry so every
    // surviving key still maps to a valid slot.
    store_[idx] = std::move(store_[last]);
    const Key moved{store_[idx].stream_id, store_[idx].msg_id};
    index_[moved] = idx;
  }
  store_.pop_back();
}

bool Reassembler::accept_data(const Frame& f, Message& out) {
  const Key key{f.stream_id, f.msg_id};
  const std::size_t idx = slot_for(key, f.stream_id, f.msg_id);
  PartialMessage& p = store_[idx];

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
  remove_slot(idx, key);
  return true;
}

std::size_t Reassembler::reset_stream(std::uint16_t stream_id) {
  // RESET lifecycle: drop every partial buffer for this stream and compact the
  // store so the freed slots are reclaimed. Partially received messages are
  // discarded and never emitted.
  //
  // Compaction shifts the surviving partials down into the holes left by the
  // dropped ones, so the index entries that point past a hole must be shifted
  // to match. We remember where the first hole opened and slide those entries
  // down to keep the map and the store in agreement.
  std::size_t survivors = 0;
  for (const PartialMessage& p : store_) {
    if (p.stream_id != stream_id) {
      ++survivors;
    }
  }

  std::vector<PartialMessage> kept;
  kept.reserve(survivors);

  std::size_t dropped = 0;
  std::size_t first_hole = 0;
  bool opened_hole = false;
  for (std::size_t i = 0; i < store_.size(); ++i) {
    if (store_[i].stream_id == stream_id) {
      if (!opened_hole) {
        first_hole = i;
        opened_hole = true;
      }
      index_.erase(Key{store_[i].stream_id, store_[i].msg_id});
      ++dropped;
      continue;
    }
    kept.push_back(std::move(store_[i]));
  }
  store_.swap(kept);

  // Slide the index entries that sat after the first hole down to track the
  // compaction. Entries before the hole are unaffected.
  if (opened_hole) {
    for (auto& entry : index_) {
      if (entry.second > first_hole) {
        entry.second -= 1;
      }
    }
  }

  return dropped;
}

}  // namespace streamcodec
}  // namespace bw
