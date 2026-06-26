// bytewright - binpack libFuzzer entry point.
//
// We parse the input and then deliberately exercise the *deep* paths: recurse
// over every record of every section, touch each typed value, and finally run
// the formatter so summarize()'s code executes too. Malformed inputs surface as
// bw::common::ParseError (a std::exception) and are simply rejected.
#include <cstddef>
#include <cstdint>
#include <string>

#include "binpack/binpack.hpp"

namespace {

// Walk a record tree, touching every member so the optimizer cannot elide the
// decode work and so the fuzzer is credited for reaching nested structure.
std::size_t walk_record(const bw::binpack::Record& rec) {
  std::size_t acc = static_cast<std::size_t>(rec.tag);
  acc += rec.group ? 1u : 0u;
  switch (rec.tag) {
    case bw::binpack::RecordTag::kInt:
      acc += static_cast<std::size_t>(rec.i64);
      break;
    case bw::binpack::RecordTag::kUint:
      acc += static_cast<std::size_t>(rec.u64);
      break;
    case bw::binpack::RecordTag::kFloat:
      acc += static_cast<std::size_t>(rec.f64);
      break;
    case bw::binpack::RecordTag::kString:
      acc += rec.str.size();
      break;
    case bw::binpack::RecordTag::kBlob:
      acc += rec.blob.size();
      break;
    case bw::binpack::RecordTag::kKeyval:
      acc += rec.key.size();
      // fallthrough to children
    case bw::binpack::RecordTag::kArray:
    case bw::binpack::RecordTag::kGroup:
      for (const auto& child : rec.children) acc += walk_record(child);
      break;
  }
  return acc;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  try {
    auto c = bw::binpack::parse(data, size);

    std::size_t acc = 0;
    acc += c.checksum_ok ? 1u : 0u;
    for (const auto& section : c.sections) {
      acc += section.records.size();
      for (const auto& rec : section.records) {
        acc += walk_record(rec);
      }
    }
    for (const auto& kv : c.metadata) {
      acc += kv.first.size();
      acc += walk_record(kv.second);
    }

    // Run the formatter so its code path is fuzzed as well.
    volatile std::size_t sink = bw::binpack::summarize(c).size();
    (void)sink;
    (void)acc;
  } catch (const std::exception&) {
    // Malformed input: reject, never abort.
  }
  return 0;
}
