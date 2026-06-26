// bytewright - streamcodec message reassembler
//
// A message is identified by (stream_id, msg_id) and may arrive as one shot or
// as several FRAG frames, possibly out of order by frag_offset. The reassembler
// keeps, per in-flight message, a growable byte buffer plus the set of filled
// byte ranges. When FIN is seen and the filled ranges cover [0, total_len) the
// message is complete and its contiguous payload can be emitted.
//
// Storage model: in-flight partials live in a contiguous `store_` vector for
// cache-friendly iteration, and a `index_` map gives O(log n) lookup from a
// (stream_id, msg_id) key to the partial's slot. Completing a message removes
// its slot with a swap-and-pop that keeps the index consistent. A RESET compacts
// the store to reclaim the slots held by a stream's in-flight messages.
#ifndef BYTEWRIGHT_STREAMCODEC_REASSEMBLER_HPP
#define BYTEWRIGHT_STREAMCODEC_REASSEMBLER_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "streamcodec/frame.hpp"

namespace bw {
namespace streamcodec {

// A completed, contiguous message handed back to the decoder.
struct Message {
  std::uint16_t stream_id = 0;
  std::uint32_t msg_id = 0;
  bool compressed = false;  // COMPRESSED flag was seen (payload left as-is)
  std::vector<std::uint8_t> data;
};

// In-flight reassembly state for a single (stream_id, msg_id). The key is stored
// alongside the buffer so the store can be compacted without consulting the
// index map.
struct PartialMessage {
  std::uint16_t stream_id = 0;
  std::uint32_t msg_id = 0;
  std::vector<std::uint8_t> buffer;  // grows to at least max end offset seen
  std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;  // [begin,end)
  std::uint32_t high_water = 0;  // highest end offset placed so far
  std::uint32_t total_len = 0;   // fixed once FIN is observed
  bool have_fin = false;
  bool compressed = false;
};

class Reassembler {
 public:
  // Apply a DATA frame to its message. Returns true and fills `out` when the
  // frame completed the message; otherwise returns false and keeps the partial
  // state. Never throws on out-of-order or overlapping fragments - it places
  // bytes and merges ranges defensively.
  bool accept_data(const Frame& f, Message& out);

  // Drop every in-flight message for `stream_id` (RESET lifecycle). Returns the
  // number of partial messages discarded.
  std::size_t reset_stream(std::uint16_t stream_id);

  // Number of in-flight partial messages currently held (test/diagnostic use).
  std::size_t in_flight() const noexcept { return store_.size(); }

 private:
  using Key = std::pair<std::uint16_t, std::uint32_t>;  // (stream_id, msg_id)

  // True once the merged ranges cover [0, total_len) with no holes.
  static bool is_complete(const PartialMessage& p);

  // Insert [begin, end) into p.ranges, coalescing touching/overlapping spans.
  static void add_range(PartialMessage& p, std::uint32_t begin,
                        std::uint32_t end);

  // Find the slot for `key`, creating a fresh partial at the end if absent.
  std::size_t slot_for(const Key& key, std::uint16_t stream_id,
                       std::uint32_t msg_id);

  // Remove the slot at `idx` (whose key is `key`) via swap-and-pop, keeping the
  // index map consistent for the element that gets moved into the hole.
  void remove_slot(std::size_t idx, const Key& key);

  std::vector<PartialMessage> store_;
  std::map<Key, std::size_t> index_;  // key -> position in store_
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_REASSEMBLER_HPP
