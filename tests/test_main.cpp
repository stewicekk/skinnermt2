// CTest entry point: runs all registered cases, prints a summary.
#include <cstdio>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "expect.hpp"

int main() {
#if defined(_DEBUG) && defined(M2RIG_HEAP_TRIAGE)
    // CRASH TRIAGE (opt-in via -DM2RIG_HEAP_TRIAGE=ON, very slow): full-heap
    // validation on every heap op + never reuse freed blocks. Catches the
    // corrupter (OOB write) or UAF at the guilty call instead of detonating
    // suites later. Default OFF: the fast path must stay green by itself.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF |
                    _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_DELAY_FREE_MEM_DF);
#endif
    int failedCases = 0;
    int failedChecks = 0;
    int total = 0;
    std::string currentSuite;
    for (const auto& c : ::m2rig::test::registry()) {
        if (c.suite != currentSuite) {
#if defined(_DEBUG) && defined(M2RIG_HEAP_TRIAGE)
            // TRIAGE-ONLY (see main): validate the CRT heap at every suite
            // boundary. A heap smash detonates late (often suites later);
            // this attributes it to the suite that actually corrupted.
            // Slow: full-heap walk per suite, hence opt-in.
            if (!currentSuite.empty() && !_CrtCheckMemory()) {
                printf("HEAP CORRUPTION detected after suite %s\n", currentSuite.c_str());
                fflush(stdout);
                _CrtDbgBreak();
            }
#endif
            currentSuite = c.suite;
            printf("[suite %s]\n", currentSuite.c_str());
            fflush(stdout);
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
        // Flush every case: a hard crash (segfault/abort) must leave the
        // exact last-completed test on record instead of a buffered gap.
        fflush(stdout);
    }
    printf("----\n%d/%d cases passed", total - failedCases, total);
    if (failedCases > 0) printf(" (%d failed checks in %d cases)", failedChecks, failedCases);
    printf("\n");
    return failedCases == 0 ? 0 : 1;
}
