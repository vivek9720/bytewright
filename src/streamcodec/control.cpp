// bytewright - streamcodec flow-control state machine implementation.
//
// All three monotonic counters (advertised, sent, acked) live in a per-stream
// FlowState. The arithmetic is deliberately saturating and order-insensitive so
// that reordered or duplicated control/ack inputs cannot drive a stream into an
// inconsistent state: window ceilings only rise, acks only advance, and `sent`
// is admitted only up to the ceiling.
#include "streamcodec/control.hpp"

#include <algorithm>

namespace bw {
namespace streamcodec {

FlowState FlowController::on_window_update(const WindowUpdate& u) {
  FlowState& s = streams_[u.stream_id];

  // The new ceiling implied by this advertisement, guarded against u64 overflow
  // on ack_point + window.
  std::uint64_t ceiling = u.ack_point;
  if (u.window > (UINT64_MAX - ceiling)) {
    ceiling = UINT64_MAX;
  } else {
    ceiling += u.window;
  }

  // Never let the ceiling shrink (ignores stale/reordered smaller updates).
  s.advertised = std::max(s.advertised, ceiling);
  refresh(s);
  return s;
}

FlowState FlowController::on_ack(const WindowAck& a) {
  FlowState& s = streams_[a.stream_id];

  // Acks only advance and can never exceed what has been sent.
  std::uint64_t acked = std::max(s.acked, a.acked_bytes);
  s.acked = std::min(acked, s.sent);
  refresh(s);
  return s;
}

std::uint64_t FlowController::try_send(std::uint16_t stream_id,
                                       std::uint64_t n) {
  FlowState& s = streams_[stream_id];

  // Admit at most the remaining credit below the advertised ceiling.
  const std::uint64_t room = s.advertised >= s.sent ? s.advertised - s.sent : 0;
  const std::uint64_t admit = std::min(n, room);
  s.sent += admit;
  refresh(s);
  return admit;
}

bool FlowController::blocked(std::uint16_t stream_id) const {
  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    // Nothing advertised yet: treat as blocked (advertised ceiling is 0).
    return true;
  }
  return it->second.blocked;
}

FlowState FlowController::state(std::uint16_t stream_id) const {
  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    FlowState empty;
    empty.blocked = true;  // unknown stream has a zero ceiling
    return empty;
  }
  return it->second;
}

}  // namespace streamcodec
}  // namespace bw
