/* Minimal stand-in for the parts of googletest mi_builder_test.cpp uses.
 * gtest is not installed in the build container. */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>
#include <functional>
namespace testing {
class Test { public: virtual ~Test() {} virtual void SetUp() {} virtual void TearDown() {} virtual void TestBody() = 0; };
struct Reg { const char *name; std::function<Test*()> make; };
inline std::vector<Reg> &registry() { static std::vector<Reg> r; return r; }
inline int &failures() { static int f; return f; }
inline long &checks() { static long c; return c; }
struct Registrar { Registrar(const char *n, std::function<Test*()> m) { registry().push_back({n, m}); } };
struct Fatal {};
struct Msg {
   std::ostringstream os; bool fail, fatal;
   Msg(bool ok, bool fatal_, const char *file, int line, const char *expr) : fail(!ok), fatal(fatal_) {
      checks()++;
      if (fail) os << file << ":" << line << ": FAILED " << expr << " ";
   }
   template <class T> Msg &operator<<(const T &v) { if (fail) os << v; return *this; }
   ~Msg() noexcept(false) { if (fail) { failures()++; fprintf(stderr, "%s\n", os.str().c_str()); if (fatal) throw Fatal(); } }
};
template <class A, class B> std::string vals(const A &a, const B &b) { std::ostringstream o; o << "(" << +a << " vs " << +b << ") "; return o.str(); }
}
#define _GT_CMP(a, op, b, fatal) ::testing::Msg(((a) op (b)), fatal, __FILE__, __LINE__, #a " " #op " " #b) << ::testing::vals(a, b)
#define EXPECT_EQ(a, b) _GT_CMP(a, ==, b, false)
#define ASSERT_EQ(a, b) _GT_CMP(a, ==, b, true)
#define ASSERT_NE(a, b) ::testing::Msg(((a) != (b)), true, __FILE__, __LINE__, #a " != " #b)
#define ASSERT_GT(a, b) _GT_CMP(a, >, b, true)
#define ASSERT_TRUE(a) ::testing::Msg(!!(a), true, __FILE__, __LINE__, #a)
#define EXPECT_TRUE(a) ::testing::Msg(!!(a), false, __FILE__, __LINE__, #a)
#define TEST_F(fix, name) \
   struct fix##_##name : public fix { void TestBody() override; }; \
   static ::testing::Registrar fix##_##name##_reg(#fix "." #name, [] () -> ::testing::Test * { return new fix##_##name; }); \
   void fix##_##name::TestBody()
