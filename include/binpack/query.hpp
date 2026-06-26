// bytewright - binpack model query helpers
//
// Small, allocation-light lookups over an already-parsed Container. Everything
// here is read-only and returns pointers/optionals *into* the model, so the
// Container must outlive any returned pointer. Nothing throws: a miss is a null
// pointer or an empty optional.
//
//   * find_section / find_section_by_id  -- locate a section by type or id
//   * metadata_value                     -- look up a metadata value by key
//   * metadata_string / metadata_uint    -- typed metadata convenience accessors
//   * collect_records                    -- gather every record of a given tag,
//                                           optionally recursing into children
#ifndef BYTEWRIGHT_BINPACK_QUERY_HPP
#define BYTEWRIGHT_BINPACK_QUERY_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "binpack/model.hpp"

// <optional> is part of C++17; metadata_string/_uint return std::optional.
#include <optional>

namespace bw {
namespace binpack {

// First section of the given type, or nullptr if none. The const and non-const
// overloads share one implementation via the usual const_cast trick on the
// const result (safe: the input is non-const).
const Section* find_section(const Container& c, SectionType type) noexcept;
Section* find_section(Container& c, SectionType type) noexcept;

// First section with the given id (ids are not required to be unique; this
// returns the first match in table order), or nullptr.
const Section* find_section_by_id(const Container& c,
                                  std::uint16_t id) noexcept;
Section* find_section_by_id(Container& c, std::uint16_t id) noexcept;

// All sections of a given type, in table order. Pointers reference into `c`.
std::vector<const Section*> find_sections(const Container& c,
                                          SectionType type);

// Look up a metadata value node by key in the Container's collected metadata
// view. Returns the first match (matching how parse() collected them), or
// nullptr if the key is absent.
const Record* metadata_value(const Container& c,
                             const std::string& key) noexcept;

// Typed metadata accessors. Each returns a value only if the key exists *and*
// the value node has the expected tag; otherwise std::nullopt.
std::optional<std::string> metadata_string(const Container& c,
                                           const std::string& key);
std::optional<std::uint64_t> metadata_uint(const Container& c,
                                            const std::string& key);
std::optional<std::int64_t> metadata_int(const Container& c,
                                         const std::string& key);

// Collect pointers to every record carrying `tag`. When `recursive` is true the
// search descends into array/group/keyval children; otherwise only the
// top-level records of each section are considered. Results are in traversal
// order. Pointers reference into `c` and are invalidated if `c` is mutated.
std::vector<const Record*> collect_records(const Container& c, RecordTag tag,
                                           bool recursive = true);

// Collect every record within a single section subtree (top-level records and,
// when recursive, their descendants) that carries `tag`.
std::vector<const Record*> collect_records(const Section& section,
                                           RecordTag tag,
                                           bool recursive = true);

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_QUERY_HPP
