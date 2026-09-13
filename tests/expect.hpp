#pragma once
// Dependency-free test harness: TEST/suite registration + CHECK macros.
// Each test returns the number of failed checks; the runner reports totals.
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace m2rig::test {

struct Case {
    std::string suite;
    std::string name;
    std::function<int()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> kCases;
    return kCases;
}

struct Registrar {
    Registrar(std::string suite, std::string name, std::function<int()> fn) {
        registry().push_back({std::move(suite), std::move(name), std::move(fn)});
    }
};

}  // namespace m2rig::test

#define M2RIG_TEST(suite, name)                                                     \
    static int suite##_##name##_fn();                                               \
    static ::m2rig::test::Registrar suite##_##name##_reg(#suite, #name,              \
                                                         suite##_##name##_fn);      \
    static int suite##_##name##_fn()

#define M2RIG_CHECK_CTX (std::string(__FILE__) + ":" + std::to_string(__LINE__))

#define CHECK_TRUE(cond)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            printf("    FAIL %s: CHECK_TRUE(%s)\n", M2RIG_CHECK_CTX.c_str(), #cond);  \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_FALSE(cond)                                                            \
    do {                                                                             \
        if (cond) {                                                                  \
            printf("    FAIL %s: CHECK_FALSE(%s)\n", M2RIG_CHECK_CTX.c_str(), #cond); \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        if (!((a) == (b))) {                                                            \
            printf("    FAIL %s: CHECK_EQ(%s, %s)\n", M2RIG_CHECK_CTX.c_str(), #a, #b);  \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                              \
    do {                                                                                   \
        const double da = static_cast<double>(a);                                          \
        const double db = static_cast<double>(b);                                          \
        if (std::fabs(da - db) > static_cast<double>(eps)) {                               \
            printf("    FAIL %s: CHECK_NEAR(%s=%f, %s=%f, eps=%f)\n",                      \
                   M2RIG_CHECK_CTX.c_str(), #a, da, #b, db, static_cast<double>(eps));     \
            ++failures;                                                                    \
        }                                                                                  \
    } while (0)
