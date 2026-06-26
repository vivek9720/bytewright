#include <cstddef>
#include <cstdint>

#include "cfgscript/cfgscript.hpp"

// Entry point for libFuzzer / ClusterFuzzLite. Any byte sequence is valid input
// to attempt to parse; malformed input throws ParseError (a std::exception),
// which we swallow so the harness treats it as "rejected" rather than a crash.
// On success we also run summarize() so the printer path is exercised.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  try {
    auto doc = bw::cfgscript::parse(data, size);
    volatile std::size_t sink = bw::cfgscript::summarize(doc).size();
    (void)sink;
  } catch (const std::exception&) {
    // Expected for malformed input.
  }
  return 0;
}
