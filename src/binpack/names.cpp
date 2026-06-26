// bytewright - binpack human-readable names for tags and section types.
#include "binpack/model.hpp"

namespace bw {
namespace binpack {

const char* tag_name(RecordTag tag) noexcept {
  switch (tag) {
    case RecordTag::kInt:    return "INT";
    case RecordTag::kUint:   return "UINT";
    case RecordTag::kFloat:  return "FLOAT";
    case RecordTag::kString: return "STRING";
    case RecordTag::kBlob:   return "BLOB";
    case RecordTag::kArray:  return "ARRAY";
    case RecordTag::kGroup:  return "GROUP";
    case RecordTag::kKeyval: return "KEYVAL";
  }
  return "?";
}

const char* section_type_name(SectionType type) noexcept {
  switch (type) {
    case SectionType::kFree:     return "FREE";
    case SectionType::kData:     return "DATA";
    case SectionType::kMetadata: return "METADATA";
    case SectionType::kIndex:    return "INDEX";
    case SectionType::kStrings:  return "STRINGS";
  }
  return "UNKNOWN";
}

}  // namespace binpack
}  // namespace bw
