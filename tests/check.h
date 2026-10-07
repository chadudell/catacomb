// A tiny test harness: TEST(name) { CHECK(cond); CHECK_NEAR(a, b, tol); }
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace check {

struct Test {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}

inline int& failures() {
  static int n = 0;
  return n;
}

struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const char* what) {
  std::printf("    FAIL %s:%d  %s\n", file, line, what);
  failures()++;
}

} // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                                  \
  static void CHECK_CAT(test_, __LINE__)();                                         \
  static check::Register CHECK_CAT(reg_, __LINE__)(name, CHECK_CAT(test_, __LINE__)); \
  static void CHECK_CAT(test_, __LINE__)()

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) check::fail(__FILE__, __LINE__, #cond);    \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                          \
  do {                                                                                 \
    const double a_ = (a), b_ = (b);                                                   \
    if (!(std::abs(a_ - b_) <= (tol))) {                                               \
      char buf_[256];                                                                  \
      std::snprintf(buf_, sizeof buf_, "%s ≈ %s  (%g vs %g, tol %g)", #a, #b, a_, b_, (double)(tol)); \
      check::fail(__FILE__, __LINE__, buf_);                                           \
    }                                                                                  \
  } while (0)
