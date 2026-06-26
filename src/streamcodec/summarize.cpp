// bytewright - streamcodec human-readable summary.
//
// Renders a DecodeResult as a multi-line report: top-level frame/message
// counters, each completed message with a short hex preview, the per-stream
// stats table, and the control/ack records.
#include <cstddef>
#include <string>

#include "common/hexdump.hpp"
#include "streamcodec/streamcodec.hpp"

namespace bw {
namespace streamcodec {

namespace {

// A compact, single-line hex preview of up to `limit` payload bytes.
std::string preview(const std::vector<std::uint8_t>& data, std::size_t limit) {
  std::string out;
  const std::size_t n = data.size() < limit ? data.size() : limit;
  for (std::size_t i = 0; i < n; ++i) {
    if (i) out += ' ';
    out += common::to_hex(data[i], 2);
  }
  if (data.size() > limit) {
    out += " ...";
  }
  return out;
}

}  // namespace

std::string summarize(const DecodeResult& r) {
  std::string out;

  out += "streamcodec decode\n";
  out += "  frames seen      : " + std::to_string(r.frames_seen) + "\n";
  out += "  messages emitted : " + std::to_string(r.messages.size()) + "\n";
  out += "  control frames   : " + std::to_string(r.control_frames) + "\n";
  out += "  ack frames       : " + std::to_string(r.ack_frames) + "\n";
  out += "  reset frames     : " + std::to_string(r.reset_frames) + "\n";
  out += "  header crc ok    : " + std::to_string(r.header_crc_ok) + "\n";
  out += "  header crc bad   : " + std::to_string(r.header_crc_bad) + "\n";

  out += "messages:\n";
  for (const Message& m : r.messages) {
    out += "  stream " + std::to_string(m.stream_id) + " msg " +
           std::to_string(m.msg_id) + " size " +
           std::to_string(m.data.size());
    out += m.compressed ? " [compressed]" : "";
    out += "\n    bytes: " + preview(m.data, 16) + "\n";
  }

  out += "streams:\n";
  for (const StreamStats& s : r.stats) {
    out += "  id " + std::to_string(s.stream_id) +
           " frames=" + std::to_string(s.frames) +
           " gaps=" + std::to_string(s.gaps) +
           " dups=" + std::to_string(s.duplicates) +
           " resets=" + std::to_string(s.resets) + "\n";
  }

  if (!r.controls.empty()) {
    out += "controls:\n";
    for (const ControlRecord& c : r.controls) {
      out += "  stream " + std::to_string(c.stream_id) +
             " ref_msg=" + std::to_string(c.ref_msg_id) +
             " window=" + std::to_string(c.window) + "\n";
    }
  }

  if (!r.acks.empty()) {
    out += "acks:\n";
    for (const AckRecord& a : r.acks) {
      out += "  stream " + std::to_string(a.stream_id) +
             " msg " + std::to_string(a.msg_id) + "\n";
    }
  }

  return out;
}

}  // namespace streamcodec
}  // namespace bw
