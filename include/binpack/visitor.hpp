// bytewright - binpack record visitor and structural validator
//
// Two related read-only facilities over a parsed Container:
//
//  * RecordVisitor: a classic visitor interface for a depth-tracked recursive
//    walk. Container records (array/group/keyval) bracket their children with
//    enter_*/leave_* callbacks; scalar records get a single visit_* callback.
//    walk() drives the traversal; subclasses override only what they care about
//    (the base methods are no-ops). Useful for collecting, counting, or pretty-
//    printing without re-implementing the recursion each time.
//
//  * validate(): a non-throwing structural check that returns a report of issues
//    (empty groups, duplicate metadata keys, oversized blobs, declared
//    item_count disagreeing with the actual record count, malformed keyvals,
//    excessive depth). "Invalid" is data, not an exception: callers inspect the
//    report and decide what to do.
#ifndef BYTEWRIGHT_BINPACK_VISITOR_HPP
#define BYTEWRIGHT_BINPACK_VISITOR_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "binpack/model.hpp"

namespace bw {
namespace binpack {

// Visitor over the record tree. `depth` is 0 for a section's top-level records
// and increases by one per level of nesting. `index` is the position of the
// record within its immediate parent (or section) for diagnostics.
class RecordVisitor {
 public:
  virtual ~RecordVisitor() = default;

  // Called once before walking a section's record stream and once after.
  virtual void enter_section(const Section& section, std::size_t index) {
    (void)section;
    (void)index;
  }
  virtual void leave_section(const Section& section, std::size_t index) {
    (void)section;
    (void)index;
  }

  // Scalar records.
  virtual void visit_scalar(const Record& rec, std::size_t depth,
                            std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }

  // Array records: enter, then each element is visited, then leave.
  virtual void enter_array(const Record& rec, std::size_t depth,
                           std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }
  virtual void leave_array(const Record& rec, std::size_t depth,
                           std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }

  // Group records.
  virtual void enter_group(const Record& rec, std::size_t depth,
                           std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }
  virtual void leave_group(const Record& rec, std::size_t depth,
                           std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }

  // Keyval records: enter, the single value child is visited, then leave.
  virtual void enter_keyval(const Record& rec, std::size_t depth,
                            std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }
  virtual void leave_keyval(const Record& rec, std::size_t depth,
                            std::size_t index) {
    (void)rec;
    (void)depth;
    (void)index;
  }
};

// Drive `visitor` over every section and record of `c`.
void walk(const Container& c, RecordVisitor& visitor);

// Drive `visitor` over a single record subtree (and its children).
void walk_record(const Record& rec, RecordVisitor& visitor, std::size_t depth,
                 std::size_t index);

// ---- validation ---------------------------------------------------------

// Severity of a validation finding. kWarning marks something suspicious but
// still decodable; kError marks a genuine structural inconsistency.
enum class IssueSeverity {
  kWarning,
  kError,
};

// Distinct, machine-branchable kinds of finding so callers don't string-match.
enum class IssueKind {
  kEmptyGroup,            // a GROUP record with no children
  kEmptyArray,           // an ARRAY record declaring/holding zero elements
  kDuplicateMetadataKey, // the same metadata key appears more than once
  kOversizedBlob,        // a BLOB exceeds the configured byte limit
  kItemCountMismatch,    // section item_count != actual top-level record count
  kMalformedKeyval,      // a KEYVAL without exactly one child or with empty key
  kDepthExceeded,        // nesting deeper than the configured advisory limit
  kEmptyMetadataKey,     // a metadata keyval with an empty key string
};

const char* issue_kind_name(IssueKind kind) noexcept;

// One finding. `section_index` locates the offending section; `path` is a
// human-readable trail to the record (e.g. "section[1]/group[0]/array[2]").
struct Issue {
  IssueSeverity severity = IssueSeverity::kWarning;
  IssueKind kind = IssueKind::kEmptyGroup;
  std::size_t section_index = 0;
  std::string path;
  std::string detail;
};

// Tunables for validate(). Defaults are deliberately lenient so a typical valid
// container produces no findings.
struct ValidateOptions {
  std::size_t max_blob_bytes = 1u << 20;  // 1 MiB -> kOversizedBlob above this
  std::size_t max_depth = 32;             // advisory nesting limit
  bool check_item_count = true;           // compare item_count to record count
};

// Report aggregate. `ok()` is true iff there are no kError-severity issues;
// warnings alone still leave a container "ok".
struct ValidationReport {
  std::vector<Issue> issues;

  bool ok() const noexcept {
    for (const Issue& i : issues) {
      if (i.severity == IssueSeverity::kError) return false;
    }
    return true;
  }
  std::size_t error_count() const noexcept {
    std::size_t n = 0;
    for (const Issue& i : issues) {
      if (i.severity == IssueSeverity::kError) ++n;
    }
    return n;
  }
  std::size_t warning_count() const noexcept {
    std::size_t n = 0;
    for (const Issue& i : issues) {
      if (i.severity == IssueSeverity::kWarning) ++n;
    }
    return n;
  }
};

ValidationReport validate(const Container& c);
ValidationReport validate(const Container& c, const ValidateOptions& opts);

}  // namespace binpack
}  // namespace bw

#endif  // BYTEWRIGHT_BINPACK_VISITOR_HPP
