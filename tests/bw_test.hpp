// bytewright - minimal header-only test harness
//
// We deliberately avoid an external test framework so the repository has zero
// third-party dependencies and builds from a clean checkout. Each test
// translation unit registers cases with BW_TEST and ends with BW_TEST_MAIN().
#ifndef BYTEWRIGHT_TESTS_BW_TEST_HPP
#define BYTEWRIGHT_TESTS_BW_TEST_HPP

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace bw {
namespace test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failure_count() {
  static int failures = 0;
  return failures;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

class CheckFailure : public std::exception {
 public:
  explicit CheckFailure(std::string msg) : msg_(std::move(msg)) {}
  const char* what() const noexcept override { return msg_.c_str(); }

 private:
  std::string msg_;
};

inline void fail(const std::string& where, const std::string& expr) {
  throw CheckFailure(where + ": check failed: " + expr);
}

inline int run_all() {
  int passed = 0;
  for (const Case& c : registry()) {
    try {
      c.fn();
      std::printf("[ ok ] %s\n", c.name);
      ++passed;
    } catch (const std::exception& e) {
      std::printf("[FAIL] %s\n       %s\n", c.name, e.what());
      ++failure_count();
    } catch (...) {
      std::printf("[FAIL] %s\n       non-standard exception\n", c.name);
      ++failure_count();
    }
  }
  std::printf("----\n%d passed, %d failed (%zu total)\n", passed,
              failure_count(), registry().size());
  return failure_count() == 0 ? 0 : 1;
}

}  // namespace test
}  // namespace bw

#define BW_CONCAT_INNER(a, b) a##b
#define BW_CONCAT(a, b) BW_CONCAT_INNER(a, b)

#define BW_TEST(name)                                                      \
  static void name();                                                      \
  static ::bw::test::Registrar BW_CONCAT(bw_reg_, name)(#name, &name);     \
  static void name()

#define BW_WHERE (std::string(__FILE__) + ":" + std::to_string(__LINE__))

#define BW_CHECK(cond)                                  \
  do {                                                  \
    if (!(cond)) ::bw::test::fail(BW_WHERE, #cond);     \
  } while (0)

#define BW_CHECK_EQ(a, b)                                                  \
  do {                                                                     \
    if (!((a) == (b)))                                                     \
      ::bw::test::fail(BW_WHERE, std::string(#a) + " == " + #b);           \
  } while (0)

#define BW_CHECK_THROWS(stmt)                                              \
  do {                                                                     \
    bool threw = false;                                                    \
    try {                                                                  \
      stmt;                                                                \
    } catch (...) {                                                        \
      threw = true;                                                        \
    }                                                                      \
    if (!threw)                                                            \
      ::bw::test::fail(BW_WHERE, std::string("expected throw: ") + #stmt); \
  } while (0)

#define BW_TEST_MAIN()                       \
  int main() { return ::bw::test::run_all(); }

#endif  // BYTEWRIGHT_TESTS_BW_TEST_HPP
