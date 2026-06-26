// bytewright - binpack human-readable dump
//
// summarize() renders a parsed Container as an indented, multi-line tree. It is
// also (deliberately) the formatter the fuzz harness drives, so it has to be
// total over any model parse() can produce: every tag, every nesting depth.
#include "binpack/binpack.hpp"

#include <cstdio>
#include <sstream>
#include <string>

#include "common/hexdump.hpp"

namespace bw {
namespace binpack {

namespace {

void indent(std::ostringstream& out, int depth) {
  for (int i = 0; i < depth; ++i) out << "  ";
}

// Render a double without locale surprises and without dragging in <iomanip>.
std::string fmt_double(double d) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%g", d);
  return std::string(buf);
}

void dump_record(std::ostringstream& out, const Record& rec, int depth) {
  indent(out, depth);
  out << "- " << tag_name(rec.tag);
  if (rec.group) out << " (group)";

  switch (rec.tag) {
    case RecordTag::kInt:
      out << ": " << rec.i64 << "\n";
      break;
    case RecordTag::kUint:
      out << ": " << rec.u64 << "\n";
      break;
    case RecordTag::kFloat:
      out << ": " << fmt_double(rec.f64) << "\n";
      break;
    case RecordTag::kString:
      out << ": \"" << common::escape(rec.str) << "\"\n";
      break;
    case RecordTag::kBlob: {
      out << ": " << rec.blob.size() << " bytes";
      // Show a short hex preview of the leading bytes so dumps stay compact.
      const std::size_t preview = rec.blob.size() < 8 ? rec.blob.size() : 8;
      if (preview > 0) {
        out << " [";
        for (std::size_t i = 0; i < preview; ++i) {
          if (i) out << " ";
          out << common::to_hex(rec.blob[i], 2);
        }
        if (rec.blob.size() > preview) out << " ...";
        out << "]";
      }
      out << "\n";
      break;
    }
    case RecordTag::kArray:
      out << ": " << rec.children.size() << " elements\n";
      for (const Record& child : rec.children) {
        dump_record(out, child, depth + 1);
      }
      break;
    case RecordTag::kGroup:
      out << ": " << rec.children.size() << " records\n";
      for (const Record& child : rec.children) {
        dump_record(out, child, depth + 1);
      }
      break;
    case RecordTag::kKeyval:
      out << ": key=\"" << common::escape(rec.key) << "\"\n";
      for (const Record& child : rec.children) {
        dump_record(out, child, depth + 1);
      }
      break;
  }
}

}  // namespace

std::string summarize(const Container& c) {
  std::ostringstream out;

  out << "BPK1 container\n";
  out << "  version       : " << c.version << "\n";
  out << "  flags         : " << common::to_hex(c.flags, 4)
      << (c.has_metadata ? " has_metadata" : "")
      << (c.has_trailer_crc ? " has_trailer_crc" : "") << "\n";
  out << "  checksum_ok   : " << (c.checksum_ok ? "true" : "false") << "\n";
  out << "  header_crc_ok : " << (c.header_checksum_ok ? "true" : "false")
      << "\n";
  if (c.has_trailer_crc) {
    out << "  trailer_crc_ok: " << (c.trailer_checksum_ok ? "true" : "false")
        << "\n";
  }
  out << "  sections      : " << c.sections.size() << "\n";

  for (std::size_t i = 0; i < c.sections.size(); ++i) {
    const Section& s = c.sections[i];
    out << "\n";
    out << "section[" << i << "] " << section_type_name(s.type)
        << " id=" << s.id << " flags=" << common::to_hex(s.flags, 4)
        << " item_count=" << s.item_count
        << " records=" << s.records.size()
        << " body_crc_ok=" << (s.body_checksum_ok ? "true" : "false") << "\n";
    for (const Record& rec : s.records) {
      dump_record(out, rec, 1);
    }
  }

  if (!c.metadata.empty()) {
    out << "\nmetadata (" << c.metadata.size() << " entries)\n";
    for (const auto& kv : c.metadata) {
      out << "  " << common::escape(kv.first) << " = ";
      // Render the value node inline for the common scalar cases; fall back to
      // the tag name for nested values.
      const Record& v = kv.second;
      switch (v.tag) {
        case RecordTag::kInt:    out << v.i64; break;
        case RecordTag::kUint:   out << v.u64; break;
        case RecordTag::kFloat:  out << fmt_double(v.f64); break;
        case RecordTag::kString: out << "\"" << common::escape(v.str) << "\""; break;
        case RecordTag::kBlob:   out << "<blob " << v.blob.size() << ">"; break;
        default:                 out << "<" << tag_name(v.tag) << ">"; break;
      }
      out << "\n";
    }
  }

  return out.str();
}

}  // namespace binpack
}  // namespace bw
