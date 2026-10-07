#include "check.h"

int main() {
  int failedTests = 0;
  for (const auto& t : check::registry()) {
    const int before = check::failures();
    t.fn();
    const bool ok = check::failures() == before;
    if (!ok) failedTests++;
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", t.name);
  }
  std::printf("\n%zu tests, %d failed\n", check::registry().size(), failedTests);
  return failedTests == 0 ? 0 : 1;
}
