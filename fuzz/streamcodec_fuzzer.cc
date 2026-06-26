#include <cstddef>
#include <cstdint>

#include "streamcodec/streamcodec.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  try {
    auto r = bw::streamcodec::decode(data, size);
    // summarize() walks the messages, per-stream stats, controls and acks, so
    // calling it exercises the full decode + reassembly + state output path.
    volatile std::size_t sink = bw::streamcodec::summarize(r).size();
    (void)sink;
  } catch (const std::exception&) {
    // Hard framing errors (bad sync/version, truncation) are expected on random
    // input and must not be treated as crashes.
  }
  return 0;
}
