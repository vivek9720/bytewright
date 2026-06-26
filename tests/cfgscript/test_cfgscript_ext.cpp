// bytewright - cfgscript extension tests (serializer, query, schema, flatten).
//
// These exercise the post-parse modules layered on top of the evaluated
// Document model. They register into the same shared test registry as
// test_cfgscript.cpp, which already provides BW_TEST_MAIN(); this file must NOT
// define another main().
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bw_test.hpp"
#include "cfgscript/cfgscript.hpp"
#include "cfgscript/flatten.hpp"
#include "cfgscript/model.hpp"
#include "cfgscript/query.hpp"
#include "cfgscript/schema.hpp"
#include "cfgscript/serializer.hpp"

namespace {

using bw::cfgscript::Document;
using bw::cfgscript::DiffEntry;
using bw::cfgscript::DiffKind;
using bw::cfgscript::FlatEntry;
using bw::cfgscript::QueryResult;
using bw::cfgscript::Schema;
using bw::cfgscript::Value;
using bw::cfgscript::ValueType;
using bw::cfgscript::Violation;
using bw::cfgscript::ViolationKind;

Document parse_text(const std::string& s) {
  return bw::cfgscript::parse(reinterpret_cast<const std::uint8_t*>(s.data()),
                              s.size());
}

const std::string kSample =
    "name = \"svc\";\n"
    "retries = 3;\n"
    "ratio = 1.5;\n"
    "enabled = true;\n"
    "server \"main\" {\n"
    "  port = 8080;\n"
    "  hosts = [\"a\", \"b\", \"c\"];\n"
    "  tls {\n"
    "    enabled = true;\n"
    "    mode = \"strict\";\n"
    "  }\n"
    "}\n"
    "meta = { owner = \"ops\"; tier = 2; };\n";

// Count how many violations of a given kind / path appear.
bool has_violation(const std::vector<Violation>& vs, ViolationKind kind,
                   const std::string& path) {
  for (const Violation& v : vs) {
    if (v.kind == kind && v.path == path) return true;
  }
  return false;
}

bool has_diff(const std::vector<DiffEntry>& ds, DiffKind kind,
              const std::string& path) {
  for (const DiffEntry& d : ds) {
    if (d.kind == kind && d.path == path) return true;
  }
  return false;
}

}  // namespace

// --- serializer ----------------------------------------------------------

BW_TEST(serializer_output_is_nonempty_and_stable) {
  Document doc = parse_text(kSample);
  std::string a = bw::cfgscript::serialize(doc);
  BW_CHECK(!a.empty());

  // Deterministic: serializing the same document twice gives the same bytes.
  std::string b = bw::cfgscript::serialize(doc);
  BW_CHECK_EQ(a, b);

  // Spot-check structural markers made it into the canonical text.
  BW_CHECK(a.find("server \"main\" {") != std::string::npos);
  BW_CHECK(a.find("port = 8080;") != std::string::npos);
  BW_CHECK(a.find("tls {") != std::string::npos);
}

BW_TEST(serializer_round_trips_to_equivalent_document) {
  Document original = parse_text(kSample);
  std::string text = bw::cfgscript::serialize(original);

  // The canonical text must itself parse, and re-serializing the reparsed
  // document must reproduce identical bytes (a fixed point).
  Document reparsed = parse_text(text);
  std::string text2 = bw::cfgscript::serialize(reparsed);
  BW_CHECK_EQ(text, text2);
}

BW_TEST(serializer_escapes_strings) {
  Document doc = parse_text("s = \"line1\\nline2\\t\\\"q\\\"\";\n");
  std::string text = bw::cfgscript::serialize(doc);
  // The newline/tab/quote must come back out as escapes, not raw bytes.
  BW_CHECK(text.find("\\n") != std::string::npos);
  BW_CHECK(text.find("\\t") != std::string::npos);
  BW_CHECK(text.find("\\\"") != std::string::npos);
  // And no raw newline should have leaked inside the quoted literal: the only
  // newline characters are the statement terminators added by the printer.
  Document reparsed = parse_text(text);
  BW_CHECK_EQ(reparsed.nodes.size(), static_cast<std::size_t>(1));
}

// --- query ---------------------------------------------------------------

BW_TEST(query_resolves_nested_and_array_paths) {
  Document doc = parse_text(kSample);

  BW_CHECK(bw::cfgscript::query(doc, "name").has_value());
  BW_CHECK_EQ(bw::cfgscript::find(doc, "name").as_string(), std::string("svc"));
  BW_CHECK_EQ(bw::cfgscript::find(doc, "retries").as_int(), 3);

  // Block descent + nested assignment.
  BW_CHECK_EQ(bw::cfgscript::find(doc, "server.port").as_int(), 8080);
  BW_CHECK(bw::cfgscript::find(doc, "server.tls.enabled").as_bool() == true);
  BW_CHECK_EQ(bw::cfgscript::find(doc, "server.tls.mode").as_string(),
              std::string("strict"));

  // Array indexing within a block.
  BW_CHECK_EQ(bw::cfgscript::find(doc, "server.hosts[0]").as_string(),
              std::string("a"));
  BW_CHECK_EQ(bw::cfgscript::find(doc, "server.hosts[2]").as_string(),
              std::string("c"));

  // Object member access on an assignment value.
  BW_CHECK_EQ(bw::cfgscript::find(doc, "meta.owner").as_string(),
              std::string("ops"));
  BW_CHECK_EQ(bw::cfgscript::find(doc, "meta.tier").as_int(), 2);
}

BW_TEST(query_reports_missing_paths) {
  Document doc = parse_text(kSample);

  BW_CHECK(!bw::cfgscript::query(doc, "nope").has_value());
  BW_CHECK(!bw::cfgscript::query(doc, "server.missing").has_value());
  BW_CHECK(!bw::cfgscript::query(doc, "server.hosts[9]").has_value());
  BW_CHECK(!bw::cfgscript::query(doc, "meta.absent").has_value());
  // Indexing a non-array yields nothing rather than throwing.
  BW_CHECK(!bw::cfgscript::query(doc, "name[0]").has_value());

  // QueryResult fallbacks for a missing path.
  QueryResult missing = bw::cfgscript::find(doc, "nope");
  BW_CHECK(!missing.has_value());
  BW_CHECK_EQ(missing.as_int(-1), -1);
  BW_CHECK_EQ(missing.as_string("def"), std::string("def"));
}

BW_TEST(query_typed_accessor_fallbacks_on_wrong_type) {
  Document doc = parse_text("n = 7;\ns = \"hi\";\n");
  // Asking a string for an int returns the fallback, not a throw or garbage.
  BW_CHECK_EQ(bw::cfgscript::find(doc, "s").as_int(42), 42);
  // Asking an int for a string returns the fallback.
  BW_CHECK_EQ(bw::cfgscript::find(doc, "n").as_string("x"), std::string("x"));
  // Numeric cross-access: int read as double.
  BW_CHECK(bw::cfgscript::find(doc, "n").as_double() > 6.9 &&
           bw::cfgscript::find(doc, "n").as_double() < 7.1);
}

// --- schema --------------------------------------------------------------

BW_TEST(schema_flags_type_mismatch_and_missing_required) {
  Document doc = parse_text(
      "port = \"not-a-number\";\n"   // wrong type: string where int expected
      "name = \"svc\";\n");          // present, fine
  // "timeout" is required but absent.

  Schema schema;
  schema.require("port", ValueType::kInt).range(1, 65535);
  schema.require("name", ValueType::kStr);
  schema.require("timeout", ValueType::kInt);

  std::vector<Violation> vs = bw::cfgscript::validate(doc, schema);
  BW_CHECK(has_violation(vs, ViolationKind::kTypeMismatch, "port"));
  BW_CHECK(has_violation(vs, ViolationKind::kMissingRequired, "timeout"));
  // The valid field must NOT produce a violation.
  BW_CHECK(!has_violation(vs, ViolationKind::kTypeMismatch, "name"));
  BW_CHECK(!has_violation(vs, ViolationKind::kMissingRequired, "name"));
}

BW_TEST(schema_accepts_valid_document) {
  Document doc = parse_text(
      "server \"main\" {\n"
      "  port = 8080;\n"
      "  mode = \"strict\";\n"
      "}\n");

  Schema schema;
  schema.require("server.port", ValueType::kInt).range(1, 65535);
  schema.require("server.mode", ValueType::kStr)
      .enum_values({"strict", "loose"});
  schema.optional("server.banner", ValueType::kStr);  // absent but optional

  std::vector<Violation> vs = bw::cfgscript::validate(doc, schema);
  BW_CHECK(vs.empty());
}

BW_TEST(schema_flags_range_and_enum) {
  Document doc = parse_text(
      "port = 70000;\n"          // above max
      "mode = \"weird\";\n");    // not allowed

  Schema schema;
  schema.require("port", ValueType::kInt).range(1, 65535);
  schema.require("mode", ValueType::kStr).enum_values({"strict", "loose"});

  std::vector<Violation> vs = bw::cfgscript::validate(doc, schema);
  BW_CHECK(has_violation(vs, ViolationKind::kAboveMaximum, "port"));
  BW_CHECK(has_violation(vs, ViolationKind::kNotAllowed, "mode"));
}

// --- flatten / diff ------------------------------------------------------

BW_TEST(flatten_recurses_blocks_arrays_objects) {
  Document doc = parse_text(kSample);
  std::vector<FlatEntry> flat = bw::cfgscript::flatten(doc);
  BW_CHECK(!flat.empty());

  // Build a lookup to assert specific leaves were produced with stable text.
  auto value_at = [&](const std::string& path) -> std::string {
    for (const FlatEntry& e : flat) {
      if (e.path == path) return e.value;
    }
    return std::string("<absent>");
  };

  BW_CHECK_EQ(value_at("name"), std::string("\"svc\""));
  BW_CHECK_EQ(value_at("retries"), std::string("3"));
  BW_CHECK_EQ(value_at("server[main].port"), std::string("8080"));
  BW_CHECK_EQ(value_at("server[main].hosts[1]"), std::string("\"b\""));
  BW_CHECK_EQ(value_at("server[main].tls.mode"), std::string("\"strict\""));
  BW_CHECK_EQ(value_at("meta.tier"), std::string("2"));
}

BW_TEST(diff_detects_added_and_changed_keys) {
  Document a = parse_text(
      "x = 1;\n"
      "y = 2;\n"
      "z = 3;\n");
  Document b = parse_text(
      "x = 1;\n"        // unchanged
      "y = 20;\n"       // changed
      "w = 9;\n");      // added; z removed

  std::vector<DiffEntry> ds = bw::cfgscript::diff(a, b);

  BW_CHECK(has_diff(ds, DiffKind::kChanged, "y"));
  BW_CHECK(has_diff(ds, DiffKind::kAdded, "w"));
  BW_CHECK(has_diff(ds, DiffKind::kRemoved, "z"));
  // x is identical in both, so it must not appear in the diff.
  BW_CHECK(!has_diff(ds, DiffKind::kChanged, "x"));
  BW_CHECK(!has_diff(ds, DiffKind::kAdded, "x"));
  BW_CHECK(!has_diff(ds, DiffKind::kRemoved, "x"));
}

BW_TEST(diff_of_identical_documents_is_empty) {
  Document a = parse_text(kSample);
  Document b = parse_text(kSample);
  std::vector<DiffEntry> ds = bw::cfgscript::diff(a, b);
  BW_CHECK(ds.empty());
}
