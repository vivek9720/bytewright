// bytewright - minidb libFuzzer entry point.
//
// open() runs the full pipeline (header gate -> page/slot walk -> string table
// -> journal replay -> field decode), and summarize() walks every page, slot,
// resolved field, and committed record, so a single call exercises the deep
// paths. ParseError (and any std::exception) is a normal "rejected input"
// outcome and is swallowed; only a genuine crash should fail the harness.
#include <cstddef>
#include <cstdint>
#include <exception>

#include "minidb/minidb.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  try {
    auto db = bw::minidb::open(data, size);
    volatile std::size_t sink = bw::minidb::summarize(db).size();
    (void)sink;
  } catch (const std::exception&) {
    // Malformed input: rejected, not a crash.
  }
  return 0;
}
