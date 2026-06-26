// bytewright - streamcodec ordered delivery
//
// The reassembler emits completed Messages in the order they *finish*, which is
// not necessarily ascending msg_id. DeliveryQueue restores per-stream ordering:
// it accepts completed Messages and releases them in ascending msg_id order,
// buffering out-of-order arrivals until the gap ahead of them is filled.
//
// Ordering model (per stream_id, independent of other streams):
//   * The first msg_id ever seen for a stream establishes that stream's
//     starting point; delivery proceeds from there.
//   * A message whose msg_id equals the stream's next-expected id is released
//     immediately, and any buffered successors that have now become contiguous
//     are released right after it (a drain).
//   * A message ahead of the expected id is buffered (bounded by a window).
//   * A message at or below the expected id is a duplicate/late arrival and is
//     dropped, counted in `duplicates()`.
//
// Buffering is bounded: at most `window` out-of-order messages are held per
// stream. When the buffer is full and another out-of-order message arrives, the
// queue force-advances past the lowest missing id (recording a gap) so it can
// release the buffered backlog rather than stall forever.
#ifndef BYTEWRIGHT_STREAMCODEC_DELIVERY_HPP
#define BYTEWRIGHT_STREAMCODEC_DELIVERY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "streamcodec/reassembler.hpp"

namespace bw {
namespace streamcodec {

class DeliveryQueue {
 public:
  // `window` is the maximum number of out-of-order messages buffered per
  // stream. A window of zero is treated as one (strictly in-order delivery).
  explicit DeliveryQueue(std::size_t window = 64)
      : window_(window == 0 ? 1 : window) {}

  // Offer a completed message. Returns the messages that became deliverable as
  // a result, in ascending msg_id order (possibly empty, possibly several when
  // an arrival fills a gap and drains buffered successors). A duplicate/late
  // message yields an empty result and bumps duplicates().
  std::vector<Message> offer(Message msg);

  // Flush every buffered message for `stream_id` in ascending msg_id order,
  // ignoring remaining gaps (e.g. on stream teardown). Advances the expected id
  // past the released messages. Returns the released messages.
  std::vector<Message> flush_stream(std::uint16_t stream_id);

  // Total messages currently buffered out-of-order across all streams.
  std::size_t buffered() const noexcept;

  // Number of gaps the queue has skipped over (forced advances past a missing
  // id, whether due to a full window or an explicit flush leaving holes).
  std::uint64_t gaps() const noexcept { return gaps_; }

  // Number of duplicate/late messages dropped.
  std::uint64_t duplicates() const noexcept { return duplicates_; }

 private:
  // Per-stream ordering cursor plus its out-of-order buffer keyed by msg_id.
  struct StreamOrder {
    bool started = false;             // has next_expected been established?
    std::uint32_t next_expected = 0;  // the msg_id we will release next
    std::map<std::uint32_t, Message> pending;  // buffered ahead of next_expected
  };

  // Move every now-contiguous buffered message into `out`, advancing the
  // cursor across them.
  void drain(StreamOrder& s, std::vector<Message>& out);

  std::map<std::uint16_t, StreamOrder> streams_;
  std::size_t window_;
  std::uint64_t gaps_ = 0;
  std::uint64_t duplicates_ = 0;
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_DELIVERY_HPP
