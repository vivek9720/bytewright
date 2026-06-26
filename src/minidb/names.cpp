// bytewright - minidb enum -> name helpers, kept apart from decode logic so the
// summary code and tests can render tokens without pulling in the decoder.
#include "minidb/model.hpp"

namespace bw {
namespace minidb {

const char* page_type_name(PageType t) noexcept {
  switch (t) {
    case PageType::kFree:
      return "FREE";
    case PageType::kData:
      return "DATA";
    case PageType::kStrings:
      return "STRINGS";
    case PageType::kIndex:
      return "INDEX";
    case PageType::kOverflow:
      return "OVERFLOW";
  }
  return "UNKNOWN";
}

const char* field_type_name(FieldType t) noexcept {
  switch (t) {
    case FieldType::kNull:
      return "NULL";
    case FieldType::kBool:
      return "BOOL";
    case FieldType::kInt:
      return "INT";
    case FieldType::kUint:
      return "UINT";
    case FieldType::kFloat:
      return "FLOAT";
    case FieldType::kStringRef:
      return "STRING_REF";
    case FieldType::kInlineString:
      return "INLINE_STRING";
    case FieldType::kBlob:
      return "BLOB";
  }
  return "UNKNOWN";
}

const char* journal_op_name(JournalOp op) noexcept {
  switch (op) {
    case JournalOp::kBegin:
      return "BEGIN";
    case JournalOp::kInsert:
      return "INSERT";
    case JournalOp::kUpdate:
      return "UPDATE";
    case JournalOp::kDelete:
      return "DELETE";
    case JournalOp::kCommit:
      return "COMMIT";
    case JournalOp::kRollback:
      return "ROLLBACK";
  }
  return "UNKNOWN";
}

}  // namespace minidb
}  // namespace bw
