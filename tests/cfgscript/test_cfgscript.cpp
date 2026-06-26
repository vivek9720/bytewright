// bytewright - cfgscript unit tests.
//
// Drives the public facade (parse + summarize) plus enough of the model to
// assert on decoded values. Inputs are fed as raw bytes, exactly as bwdump and
// the fuzzer do.
#include <cstdint>
#include <string>

#include "bw_test.hpp"
#include "cfgscript/cfgscript.hpp"
#include "cfgscript/model.hpp"
#include "common/status.hpp"

namespace {

using bw::cfgscript::Document;
using bw::cfgscript::Node;
using bw::cfgscript::NodeKind;
using bw::cfgscript::Value;
using bw::cfgscript::ValueType;

// Parse helper: feed a std::string as bytes, mirroring the fuzzer's call.
Document parse_text(const std::string& s) {
  return bw::cfgscript::parse(reinterpret_cast<const std::uint8_t*>(s.data()),
                              s.size());
}

// Find the first top-level assignment whose leaf path component is `leaf`.
const Node* find_assign(const Document& doc, const std::string& leaf) {
  for (const Node& n : doc.nodes) {
    if (n.kind == NodeKind::kAssignment && !n.path.empty() &&
        n.path.back() == leaf) {
      return &n;
    }
  }
  return nullptr;
}

bool is_parse_error_code(const std::exception& e, bw::common::ErrorCode want) {
  const auto* pe = dynamic_cast<const bw::common::ParseError*>(&e);
  return pe != nullptr && pe->code() == want;
}

}  // namespace

BW_TEST(scalars_of_every_type) {
  Document doc = parse_text(
      "i = 42;\n"
      "neg = -7;\n"
      "f = 1.5;\n"
      "big = 1e3;\n"
      "s = \"hello\";\n"
      "yes = true;\n"
      "no = false;\n"
      "nothing = null;\n"
      "hex = 0xFF;\n");

  const Node* i = find_assign(doc, "i");
  BW_CHECK(i != nullptr);
  BW_CHECK_EQ(i->value.type, ValueType::kInt);
  BW_CHECK_EQ(i->value.i, 42);

  BW_CHECK_EQ(find_assign(doc, "neg")->value.i, -7);

  const Node* f = find_assign(doc, "f");
  BW_CHECK_EQ(f->value.type, ValueType::kFloat);
  BW_CHECK(f->value.f > 1.49 && f->value.f < 1.51);

  const Node* big = find_assign(doc, "big");
  BW_CHECK_EQ(big->value.type, ValueType::kFloat);
  BW_CHECK(big->value.f > 999.0 && big->value.f < 1001.0);

  const Node* s = find_assign(doc, "s");
  BW_CHECK_EQ(s->value.type, ValueType::kStr);
  BW_CHECK_EQ(s->value.s, std::string("hello"));

  BW_CHECK_EQ(find_assign(doc, "yes")->value.type, ValueType::kBool);
  BW_CHECK(find_assign(doc, "yes")->value.b == true);
  BW_CHECK(find_assign(doc, "no")->value.b == false);
  BW_CHECK_EQ(find_assign(doc, "nothing")->value.type, ValueType::kNull);
  BW_CHECK_EQ(find_assign(doc, "hex")->value.i, 255);
}

BW_TEST(string_escapes_decode) {
  Document doc = parse_text(
      "nl = \"a\\nb\";\n"
      "tab = \"x\\ty\";\n"
      "hexbyte = \"\\x41\\x42\";\n"
      "uni = \"\\u{1F600}\";\n"
      "quote = \"he said \\\"hi\\\"\";\n");

  const Node* nl = find_assign(doc, "nl");
  BW_CHECK_EQ(nl->value.s, std::string("a\nb"));

  BW_CHECK_EQ(find_assign(doc, "tab")->value.s, std::string("x\ty"));

  // \x41\x42 -> "AB".
  BW_CHECK_EQ(find_assign(doc, "hexbyte")->value.s, std::string("AB"));

  // U+1F600 encodes to the 4-byte UTF-8 sequence F0 9F 98 80.
  const std::string& uni = find_assign(doc, "uni")->value.s;
  BW_CHECK_EQ(uni.size(), static_cast<std::size_t>(4));
  BW_CHECK_EQ(static_cast<unsigned char>(uni[0]), 0xF0u);
  BW_CHECK_EQ(static_cast<unsigned char>(uni[1]), 0x9Fu);
  BW_CHECK_EQ(static_cast<unsigned char>(uni[2]), 0x98u);
  BW_CHECK_EQ(static_cast<unsigned char>(uni[3]), 0x80u);

  BW_CHECK_EQ(find_assign(doc, "quote")->value.s,
              std::string("he said \"hi\""));
}

BW_TEST(arrays_and_nested_objects) {
  Document doc = parse_text(
      "list = [1, 2, 3, ];\n"
      "obj = { a = 1; b = \"two\"; nested = { c = 3; }; };\n");

  const Node* list = find_assign(doc, "list");
  BW_CHECK_EQ(list->value.type, ValueType::kArray);
  BW_CHECK_EQ(list->value.array.size(), static_cast<std::size_t>(3));
  BW_CHECK_EQ(list->value.array[2].i, 3);

  const Node* obj = find_assign(doc, "obj");
  BW_CHECK_EQ(obj->value.type, ValueType::kObject);
  BW_CHECK_EQ(obj->value.object.size(), static_cast<std::size_t>(3));
  BW_CHECK_EQ(obj->value.object[0].first, std::string("a"));
  BW_CHECK_EQ(obj->value.object[0].second.i, 1);
  BW_CHECK_EQ(obj->value.object[2].first, std::string("nested"));
  BW_CHECK_EQ(obj->value.object[2].second.type, ValueType::kObject);
  BW_CHECK_EQ(obj->value.object[2].second.object[0].second.i, 3);
}

BW_TEST(nested_blocks_with_labels) {
  Document doc = parse_text(
      "server \"main\" {\n"
      "  port = 8080;\n"
      "  tls {\n"
      "    enabled = true;\n"
      "  }\n"
      "}\n");

  BW_CHECK_EQ(doc.nodes.size(), static_cast<std::size_t>(1));
  const Node& server = doc.nodes[0];
  BW_CHECK_EQ(server.kind, NodeKind::kBlock);
  BW_CHECK_EQ(server.name, std::string("server"));
  BW_CHECK(server.has_label);
  BW_CHECK_EQ(server.label, std::string("main"));
  BW_CHECK_EQ(server.children.size(), static_cast<std::size_t>(2));

  const Node& port = server.children[0];
  BW_CHECK_EQ(port.kind, NodeKind::kAssignment);
  BW_CHECK_EQ(port.value.i, 8080);

  const Node& tls = server.children[1];
  BW_CHECK_EQ(tls.kind, NodeKind::kBlock);
  BW_CHECK(!tls.has_label);
  BW_CHECK_EQ(tls.children[0].value.type, ValueType::kBool);
  BW_CHECK(tls.children[0].value.b == true);
}

BW_TEST(include_is_data_only) {
  // The include path points at a file that does not exist; parsing must succeed
  // and must not touch the filesystem.
  Document doc = parse_text("include \"/no/such/file/should/be/read.cfg\";\n");
  BW_CHECK_EQ(doc.nodes.size(), static_cast<std::size_t>(1));
  BW_CHECK_EQ(doc.nodes[0].kind, NodeKind::kInclude);
  BW_CHECK_EQ(doc.nodes[0].include_path,
              std::string("/no/such/file/should/be/read.cfg"));
}

BW_TEST(expression_precedence) {
  Document doc = parse_text(
      "a = 2 + 3 * 4 == 14;\n"     // 2 + 12 == 14 -> true
      "b = (2 + 3) * 4;\n"          // 20
      "c = 10 - 2 - 3;\n"           // left assoc -> 5
      "d = 1 << 4 | 1;\n");         // 16 | 1 -> 17

  BW_CHECK(find_assign(doc, "a")->value.type == ValueType::kBool);
  BW_CHECK(find_assign(doc, "a")->value.b == true);
  BW_CHECK_EQ(find_assign(doc, "b")->value.i, 20);
  BW_CHECK_EQ(find_assign(doc, "c")->value.i, 5);
  BW_CHECK_EQ(find_assign(doc, "d")->value.i, 17);
}

BW_TEST(ternary_and_short_circuit) {
  Document doc = parse_text(
      "t = true ? 1 : 2;\n"
      "e = false ? 1 : 2;\n"
      "andv = false && (1 / 0 == 0);\n"   // short-circuit avoids div-by-zero
      "orv = true || (1 / 0 == 0);\n");

  BW_CHECK_EQ(find_assign(doc, "t")->value.i, 1);
  BW_CHECK_EQ(find_assign(doc, "e")->value.i, 2);
  BW_CHECK(find_assign(doc, "andv")->value.b == false);
  BW_CHECK(find_assign(doc, "orv")->value.b == true);
}

BW_TEST(bitwise_and_shift) {
  Document doc = parse_text(
      "band = 0xF0 & 0x0F;\n"   // 0
      "bor = 0xF0 | 0x0F;\n"    // 255
      "bxor = 0xFF ^ 0x0F;\n"   // 0xF0
      "shl = 1 << 8;\n"         // 256
      "shr = 1024 >> 2;\n"      // 256
      "bnot = ~0;\n");          // -1

  BW_CHECK_EQ(find_assign(doc, "band")->value.i, 0);
  BW_CHECK_EQ(find_assign(doc, "bor")->value.i, 255);
  BW_CHECK_EQ(find_assign(doc, "bxor")->value.i, 0xF0);
  BW_CHECK_EQ(find_assign(doc, "shl")->value.i, 256);
  BW_CHECK_EQ(find_assign(doc, "shr")->value.i, 256);
  BW_CHECK_EQ(find_assign(doc, "bnot")->value.i, -1);
}

BW_TEST(identifier_references_across_scopes) {
  Document doc = parse_text(
      "base = 100;\n"
      "outer {\n"
      "  derived = base + 1;\n"      // references the enclosing scope
      "  inner {\n"
      "    deep = derived * 2;\n"    // references the parent block scope
      "  }\n"
      "}\n"
      "tail = base - 50;\n");        // top-level reference

  const Node& outer = doc.nodes[1];
  BW_CHECK_EQ(outer.kind, NodeKind::kBlock);
  const Node& derived = outer.children[0];
  BW_CHECK_EQ(derived.value.i, 101);
  const Node& inner = outer.children[1];
  BW_CHECK_EQ(inner.children[0].value.i, 202);

  BW_CHECK_EQ(find_assign(doc, "tail")->value.i, 50);
}

BW_TEST(string_concatenation) {
  Document doc = parse_text("greeting = \"hi \" + \"there\";\n");
  BW_CHECK_EQ(find_assign(doc, "greeting")->value.s, std::string("hi there"));
}

BW_TEST(summarize_produces_text) {
  Document doc = parse_text(
      "name = \"svc\";\n"
      "block \"L\" { x = [1, 2]; }\n"
      "include \"other.cfg\";\n");
  std::string text = bw::cfgscript::summarize(doc);
  BW_CHECK(!text.empty());
  // Spot-check that the printer rendered the include line.
  BW_CHECK(text.find("include \"other.cfg\"") != std::string::npos);
}

// --- error cases ---------------------------------------------------------

BW_TEST(unterminated_string_throws) {
  BW_CHECK_THROWS(parse_text("x = \"oops;\n"));
}

BW_TEST(division_by_zero_is_state_violation) {
  bool threw = false;
  try {
    parse_text("x = 1 / 0;\n");
  } catch (const std::exception& e) {
    threw = true;
    BW_CHECK(is_parse_error_code(e, bw::common::ErrorCode::kStateViolation));
  }
  BW_CHECK(threw);
}

BW_TEST(modulo_by_zero_is_state_violation) {
  bool threw = false;
  try {
    parse_text("x = 7 % 0;\n");
  } catch (const std::exception& e) {
    threw = true;
    BW_CHECK(is_parse_error_code(e, bw::common::ErrorCode::kStateViolation));
  }
  BW_CHECK(threw);
}

BW_TEST(unknown_identifier_is_state_violation) {
  bool threw = false;
  try {
    parse_text("x = does_not_exist + 1;\n");
  } catch (const std::exception& e) {
    threw = true;
    BW_CHECK(is_parse_error_code(e, bw::common::ErrorCode::kStateViolation));
  }
  BW_CHECK(threw);
}

BW_TEST(deep_nesting_is_depth_exceeded) {
  // A long run of opening parens cannot be balanced before the recursion guard
  // fires; the parser must report kDepthExceeded rather than overflow.
  std::string src = "x = ";
  for (int k = 0; k < 5000; ++k) src.push_back('(');
  src += "1";
  for (int k = 0; k < 5000; ++k) src.push_back(')');
  src += ";\n";

  bool threw = false;
  try {
    parse_text(src);
  } catch (const std::exception& e) {
    threw = true;
    BW_CHECK(is_parse_error_code(e, bw::common::ErrorCode::kDepthExceeded));
  }
  BW_CHECK(threw);
}

BW_TEST(deeply_nested_blocks_depth_exceeded) {
  std::string src;
  for (int k = 0; k < 2000; ++k) src += "b {";
  bool threw = false;
  try {
    parse_text(src);
  } catch (const std::exception& e) {
    threw = true;
    BW_CHECK(is_parse_error_code(e, bw::common::ErrorCode::kDepthExceeded));
  }
  BW_CHECK(threw);
}

BW_TEST(stray_close_brace_throws) {
  BW_CHECK_THROWS(parse_text("}\n"));
}

BW_TEST(missing_semicolon_throws) {
  BW_CHECK_THROWS(parse_text("x = 1\n"));
}

BW_TEST(bad_escape_throws) {
  BW_CHECK_THROWS(parse_text("x = \"\\q\";\n"));
}

BW_TEST(empty_input_is_empty_document) {
  Document doc = parse_text("");
  BW_CHECK_EQ(doc.nodes.size(), static_cast<std::size_t>(0));
  std::string text = bw::cfgscript::summarize(doc);
  BW_CHECK(!text.empty());
}

BW_TEST(comments_are_skipped) {
  Document doc = parse_text(
      "# a hash comment\n"
      "x = 1; // line comment\n"
      "/* block\n comment */ y = 2;\n");
  BW_CHECK_EQ(find_assign(doc, "x")->value.i, 1);
  BW_CHECK_EQ(find_assign(doc, "y")->value.i, 2);
}

BW_TEST_MAIN()
