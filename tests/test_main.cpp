// CTest entry point: runs all registered cases, prints a summary.
#include <cstdio>

#include "expect.hpp"

int main() {
    int failedCases = 0;
    int failedChecks = 0;
    int total = 0;
    std::string currentSuite;
    for (const auto& c : ::m2rig::test::registry()) {
        if (c.suite != currentSuite) {
            currentSuite = c.suite;
            printf("[suite %s]\n", currentSuite.c_str());
        }
        ++total;
        const int fails = c.fn();
        if (fails != 0) {
            ++failedCases;
            failedChecks += fails;
            printf("  FAIL %s (%d checks)\n", c.name.c_str(), fails);
        } else {
            printf("  ok   %s\n", c.name.c_str());
        }
    }
    printf("----\n%d/%d cases passed", total - failedCases, total);
    if (failedCases > 0) printf(" (%d failed checks in %d cases)", failedChecks, failedCases);
    printf("\n");
    return failedCases == 0 ? 0 : 1;
}
