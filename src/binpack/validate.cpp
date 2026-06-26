// bytewright - binpack structural validator
//
// validate() walks a parsed Container and collects a report of structural
// issues without throwing: "invalid" is data. The checks are intentionally
// conservative -- a normal, well-formed container produces an empty report.
//
// Checks performed:
//   * empty GROUP / empty ARRAY                         -> warning
//   * BLOB larger than ValidateOptions.max_blob_bytes   -> warning
//   * nesting deeper than ValidateOptions.max_depth      -> error
//   * malformed KEYVAL (not exactly one child)           -> error
//   * empty KEYVAL key                                   -> warning (kEmptyMetadataKey)
//   * section item_count != top-level record count       -> error
//   * duplicate metadata keys                            -> error
//
// Path strings ("section[0]/group[1]/array[2]") locate each finding.
#include "binpack/visitor.hpp"

#include <string>
#include <unordered_map>

namespace bw {
namespace binpack {

const char* issue_kind_name(IssueKind kind) noexcept {
  switch (kind) {
    case IssueKind::kEmptyGroup:            return "empty_group";
    case IssueKind::kEmptyArray:            return "empty_array";
    case IssueKind::kDuplicateMetadataKey:  return "duplicate_metadata_key";
    case IssueKind::kOversizedBlob:         return "oversized_blob";
    case IssueKind::kItemCountMismatch:     return "item_count_mismatch";
    case IssueKind::kMalformedKeyval:       return "malformed_keyval";
    case IssueKind::kDepthExceeded:         return "depth_exceeded";
    case IssueKind::kEmptyMetadataKey:      return "empty_metadata_key";
  }
  return "?";
}

namespace {

// Per-record path component, e.g. "group[2]". Kept tiny so building paths during
// the walk is cheap.
std::string component(const char* kind, std::size_t index) {
  return std::string(kind) + "[" + std::to_string(index) + "]";
}

void add_issue(ValidationReport& report, IssueSeverity sev, IssueKind kind,
               std::size_t section_index, const std::string& path,
               const std::string& detail) {
  Issue issue;
  issue.severity = sev;
  issue.kind = kind;
  issue.section_index = section_index;
  issue.path = path;
  issue.detail = detail;
  report.issues.push_back(std::move(issue));
}

// Recursively validate one record subtree. `path` already names this record.
void validate_record(const Record& rec, std::size_t section_index,
                     const std::string& path, std::size_t depth,
                     const ValidateOptions& opts, ValidationReport& report) {
  if (depth > opts.max_depth) {
    add_issue(report, IssueSeverity::kError, IssueKind::kDepthExceeded,
              section_index, path,
              "nesting depth " + std::to_string(depth) + " exceeds limit " +
                  std::to_string(opts.max_depth));
    // Don't descend further past the advisory limit.
    return;
  }

  switch (rec.tag) {
    case RecordTag::kBlob:
      if (rec.blob.size() > opts.max_blob_bytes) {
        add_issue(report, IssueSeverity::kWarning, IssueKind::kOversizedBlob,
                  section_index, path,
                  "blob of " + std::to_string(rec.blob.size()) +
                      " bytes exceeds limit " +
                      std::to_string(opts.max_blob_bytes));
      }
      break;

    case RecordTag::kArray:
      if (rec.children.empty()) {
        add_issue(report, IssueSeverity::kWarning, IssueKind::kEmptyArray,
                  section_index, path, "array has no elements");
      }
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        validate_record(rec.children[i], section_index,
                        path + "/" + component("array", i), depth + 1, opts,
                        report);
      }
      break;

    case RecordTag::kGroup:
      if (rec.children.empty()) {
        add_issue(report, IssueSeverity::kWarning, IssueKind::kEmptyGroup,
                  section_index, path, "group has no records");
      }
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        validate_record(rec.children[i], section_index,
                        path + "/" + component("group", i), depth + 1, opts,
                        report);
      }
      break;

    case RecordTag::kKeyval:
      // A keyval must carry exactly one value child.
      if (rec.children.size() != 1) {
        add_issue(report, IssueSeverity::kError, IssueKind::kMalformedKeyval,
                  section_index, path,
                  "keyval has " + std::to_string(rec.children.size()) +
                      " children (expected 1)");
      }
      if (rec.key.empty()) {
        add_issue(report, IssueSeverity::kWarning, IssueKind::kEmptyMetadataKey,
                  section_index, path, "keyval has an empty key");
      }
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        validate_record(rec.children[i], section_index,
                        path + "/" + component("keyval", i), depth + 1, opts,
                        report);
      }
      break;

    case RecordTag::kInt:
    case RecordTag::kUint:
    case RecordTag::kFloat:
    case RecordTag::kString:
      // Scalars carry no further structure to validate.
      break;
  }
}

}  // namespace

ValidationReport validate(const Container& c, const ValidateOptions& opts) {
  ValidationReport report;

  for (std::size_t si = 0; si < c.sections.size(); ++si) {
    const Section& s = c.sections[si];
    const std::string base = component("section", si);

    // item_count vs the actual number of top-level records. FREE sections hold
    // no decoded records, so we skip the comparison for them.
    if (opts.check_item_count && s.type != SectionType::kFree) {
      if (s.item_count != s.records.size()) {
        add_issue(report, IssueSeverity::kError, IssueKind::kItemCountMismatch,
                  si, base,
                  "declared item_count " + std::to_string(s.item_count) +
                      " != actual record count " +
                      std::to_string(s.records.size()));
      }
    }

    for (std::size_t i = 0; i < s.records.size(); ++i) {
      validate_record(s.records[i], si, base + "/" + component("record", i),
                      /*depth=*/0, opts, report);
    }
  }

  // Duplicate metadata keys across the collected metadata view. The first
  // occurrence is fine; each subsequent occurrence of the same key is flagged.
  std::unordered_map<std::string, std::size_t> seen;
  for (std::size_t i = 0; i < c.metadata.size(); ++i) {
    const std::string& key = c.metadata[i].first;
    auto it = seen.find(key);
    if (it != seen.end()) {
      add_issue(report, IssueSeverity::kError, IssueKind::kDuplicateMetadataKey,
                /*section_index=*/0, "metadata[" + std::to_string(i) + "]",
                "metadata key \"" + key + "\" first seen at index " +
                    std::to_string(it->second));
    } else {
      seen.emplace(key, i);
    }
  }

  return report;
}

ValidationReport validate(const Container& c) {
  return validate(c, ValidateOptions{});
}

}  // namespace binpack
}  // namespace bw
