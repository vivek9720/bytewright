// bytewright - streamcodec flow-control state machine
//
// A small per-stream window/flow tracker, driven by CONTROL (window-update) and
// ACK inputs shaped like the decoder's ControlRecord / AckRecord. It is a pure
// bookkeeping model - no IO, no threads - that answers one question per stream:
// may the sender put more bytes on the wire, or is the stream blocked because
// its advertised window is full?
//
// Model (sliding window over a byte counter, all monotonic):
//   * advertised  - the largest cumulative byte count the receiver has allowed,
//                   raised by a window-update of the form (ack_point + window).
//   * sent        - cumulative bytes the sender has handed to the stream.
//   * acked       - cumulative bytes the receiver has confirmed.
// in_flight = sent - acked. A stream is blocked when sent has reached the
// advertised ceiling (no room to send the next byte). Window updates never
// shrink the ceiling (a stale/reordered smaller advertisement is ignored), and
// acks never move backward, mirroring real flow-control robustness.
//
// The "shaped like ControlRecord/AckRecord" inputs are defined here as light
// local structs so this module does not depend on the frozen public facade; the
// fields mirror streamcodec.hpp's records.
#ifndef BYTEWRIGHT_STREAMCODEC_CONTROL_HPP
#define BYTEWRIGHT_STREAMCODEC_CONTROL_HPP

#include <cstddef>
#include <cstdint>
#include <map>

namespace bw {
namespace streamcodec {

// A window-update input: at byte position `ack_point`, the receiver advertises
// room for a further `window` bytes (ceiling = ack_point + window). Mirrors the
// shape of streamcodec.hpp's ControlRecord (stream_id + a reference + window).
struct WindowUpdate {
  std::uint16_t stream_id = 0;
  std::uint64_t ack_point = 0;  // cumulative bytes the update is anchored at
  std::uint64_t window = 0;     // additional bytes allowed beyond ack_point
};

// An acknowledgement input: the receiver confirms `acked_bytes` cumulative
// bytes for the stream. Mirrors the shape of streamcodec.hpp's AckRecord.
struct WindowAck {
  std::uint16_t stream_id = 0;
  std::uint64_t acked_bytes = 0;  // cumulative bytes confirmed received
};

// Per-stream flow snapshot, returned by FlowController::state().
struct FlowState {
  std::uint64_t advertised = 0;  // current send ceiling (cumulative bytes)
  std::uint64_t sent = 0;        // cumulative bytes offered to send
  std::uint64_t acked = 0;       // cumulative bytes acknowledged
  bool blocked = false;          // sent has reached the advertised ceiling

  // Bytes outstanding (offered but not yet acked).
  std::uint64_t in_flight() const noexcept {
    return sent >= acked ? sent - acked : 0;
  }
  // Remaining send credit before the stream blocks.
  std::uint64_t available() const noexcept {
    return advertised >= sent ? advertised - sent : 0;
  }
};

class FlowController {
 public:
  // Apply a window-update. The ceiling only ever rises (max of the current
  // ceiling and ack_point + window), so a stale smaller advertisement is a
  // no-op. Recomputes the stream's blocked flag and returns the new state.
  FlowState on_window_update(const WindowUpdate& u);

  // Apply an ack. `acked` only ever advances (a stale lower ack is ignored) and
  // is clamped to never exceed `sent`. Returns the new state.
  FlowState on_ack(const WindowAck& a);

  // Attempt to account for sending `n` more bytes on `stream_id`. Sends are
  // admitted only up to the advertised ceiling: returns the number actually
  // admitted (0..n), advancing `sent` by that amount and updating the blocked
  // flag. A return value below `n` means the stream is (now) window-limited.
  std::uint64_t try_send(std::uint16_t stream_id, std::uint64_t n);

  // True if `stream_id` is currently blocked (sent == advertised ceiling). An
  // unknown stream is treated as blocked: nothing has been advertised yet.
  bool blocked(std::uint16_t stream_id) const;

  // Read the current state for `stream_id` (default-constructed if unknown).
  FlowState state(std::uint16_t stream_id) const;

  // Number of streams currently tracked.
  std::size_t stream_count() const noexcept { return streams_.size(); }

 private:
  // Recompute the blocked flag from the window arithmetic.
  static void refresh(FlowState& s) { s.blocked = s.sent >= s.advertised; }

  std::map<std::uint16_t, FlowState> streams_;
};

}  // namespace streamcodec
}  // namespace bw

#endif  // BYTEWRIGHT_STREAMCODEC_CONTROL_HPP
