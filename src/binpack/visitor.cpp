// bytewright - binpack record visitor traversal
//
// Implements walk()/walk_record(): a depth-tracked recursive descent over a
// Container's record tree, dispatching to the appropriate RecordVisitor hook for
// each node. Container records bracket their children with enter_*/leave_* so a
// subclass can maintain its own stack; scalar records get a single visit_scalar.
//
// The traversal is total over any model parse() can produce and never throws --
// it only reads the model and calls back into the visitor.
#include "binpack/visitor.hpp"

namespace bw {
namespace binpack {

void walk_record(const Record& rec, RecordVisitor& visitor, std::size_t depth,
                 std::size_t index) {
  switch (rec.tag) {
    case RecordTag::kInt:
    case RecordTag::kUint:
    case RecordTag::kFloat:
    case RecordTag::kString:
    case RecordTag::kBlob:
      visitor.visit_scalar(rec, depth, index);
      break;

    case RecordTag::kArray:
      visitor.enter_array(rec, depth, index);
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        walk_record(rec.children[i], visitor, depth + 1, i);
      }
      visitor.leave_array(rec, depth, index);
      break;

    case RecordTag::kGroup:
      visitor.enter_group(rec, depth, index);
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        walk_record(rec.children[i], visitor, depth + 1, i);
      }
      visitor.leave_group(rec, depth, index);
      break;

    case RecordTag::kKeyval:
      visitor.enter_keyval(rec, depth, index);
      // A keyval's payload is its single value child; the parser guarantees at
      // most one, but we loop defensively so a hand-built model can't trip us.
      for (std::size_t i = 0; i < rec.children.size(); ++i) {
        walk_record(rec.children[i], visitor, depth + 1, i);
      }
      visitor.leave_keyval(rec, depth, index);
      break;
  }
}

void walk(const Container& c, RecordVisitor& visitor) {
  for (std::size_t si = 0; si < c.sections.size(); ++si) {
    const Section& s = c.sections[si];
    visitor.enter_section(s, si);
    for (std::size_t i = 0; i < s.records.size(); ++i) {
      walk_record(s.records[i], visitor, /*depth=*/0, i);
    }
    visitor.leave_section(s, si);
  }
}

}  // namespace binpack
}  // namespace bw
