// bytewright - streamcodec ordered delivery implementation.
//
// Each stream keeps a `next_expected` cursor and a std::map<msg_id, Message> of
// arrivals that landed ahead of it. An arrival at the cursor releases and then
// drains the contiguous run that follows; an arrival ahead of the cursor is
// buffered (subject to the window); an arrival behind the cursor is a duplicate.
#include "streamcodec/delivery.hpp"

#include <utility>

namespace bw {
namespace streamcodec {

void DeliveryQueue::drain(StreamOrder& s, std::vector<Message>& out) {
  // Pop buffered messages while they are contiguous with next_expected. The map
  // is ordered, so begin() is always the lowest buffered id.
  while (!s.pending.empty()) {
    auto it = s.pending.begin();
    if (it->first != s.next_expected) {
      break;  // a hole remains before the next buffered id
    }
    out.push_back(std::move(it->second));
    s.pending.erase(it);
    ++s.next_expected;
  }
}

std::vector<Message> DeliveryQueue::offer(Message msg) {
  std::vector<Message> out;
  StreamOrder& s = streams_[msg.stream_id];

  // The first message ever seen for a stream sets its starting point.
  if (!s.started) {
    s.started = true;
    s.next_expected = msg.msg_id;
  }

  // A message at or behind the cursor is a late/duplicate arrival.
  if (msg.msg_id < s.next_expected) {
    ++duplicates_;
    return out;
  }

  if (msg.msg_id == s.next_expected) {
    // In-order: release immediately, then drain any contiguous successors.
    out.push_back(std::move(msg));
    ++s.next_expected;
    drain(s, out);
    return out;
  }

  // Ahead of the cursor: buffer it. A repeat of an already-buffered id is a
  // duplicate and is dropped rather than overwriting the held copy.
  auto existing = s.pending.find(msg.msg_id);
  if (existing != s.pending.end()) {
    ++duplicates_;
    return out;
  }
  s.pending.emplace(msg.msg_id, std::move(msg));

  // Enforce the bounded window. While we hold more than `window_` out-of-order
  // messages, the id at the cursor is presumed lost: advance the cursor to the
  // lowest buffered id (recording the gap) and drain everything that has now
  // become contiguous. Each iteration strictly increases next_expected toward
  // the lowest buffered id and never leaves it unchanged, so this terminates.
  while (s.pending.size() > window_) {
    const std::uint32_t lowest = s.pending.begin()->first;
    if (lowest > s.next_expected) {
      ++gaps_;  // we skipped over at least one missing id
    }
    // Move the cursor onto the lowest buffered id and release the contiguous
    // run that starts there.
    s.next_expected = lowest;
    drain(s, out);
  }

  return out;
}

std::vector<Message> DeliveryQueue::flush_stream(std::uint16_t stream_id) {
  std::vector<Message> out;
  auto sit = streams_.find(stream_id);
  if (sit == streams_.end()) {
    return out;
  }
  StreamOrder& s = sit->second;
  // Release everything in ascending id order. Each non-contiguous step past the
  // cursor counts as a gap.
  while (!s.pending.empty()) {
    auto it = s.pending.begin();
    if (it->first > s.next_expected) {
      ++gaps_;
    }
    s.next_expected = it->first + 1;
    out.push_back(std::move(it->second));
    s.pending.erase(it);
  }
  return out;
}

std::size_t DeliveryQueue::buffered() const noexcept {
  std::size_t n = 0;
  for (const auto& kv : streams_) {
    n += kv.second.pending.size();
  }
  return n;
}

}  // namespace streamcodec
}  // namespace bw
