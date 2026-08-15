#pragma once

// Minimal in-tree test framework — no external dependency (no
// FetchContent/Catch2), per project preference for a build with as few
// moving parts as possible. Each test .cpp includes this, registers tests
// with TEST(name){...}, and ends with MINITEST_MAIN().

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct AssertionFailure {
    std::string message;
};

inline int runAll() {
    int failures = 0;
    for (const auto& test : registry()) {
        std::printf("[ RUN      ] %s\n", test.name.c_str());
        try {
            test.fn();
            std::printf("[       OK ] %s\n", test.name.c_str());
        } catch (const AssertionFailure& f) {
            std::printf("[  FAILED  ] %s: %s\n", test.name.c_str(), f.message.c_str());
            ++failures;
        } catch (const std::exception& e) {
            std::printf("[  FAILED  ] %s: unexpected exception: %s\n", test.name.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("%zu test(s), %d failed\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace minitest

#define MINITEST_CONCAT_INNER(a, b) a##b
#define MINITEST_CONCAT(a, b) MINITEST_CONCAT_INNER(a, b)

#define TEST(name)                                                                            \
    void MINITEST_CONCAT(minitest_fn_, name)();                                                \
    static ::minitest::Registrar MINITEST_CONCAT(minitest_reg_, name)(                          \
        #name, MINITEST_CONCAT(minitest_fn_, name));                                            \
    void MINITEST_CONCAT(minitest_fn_, name)()

#define MINITEST_FAIL(msg)                                                                     \
    throw ::minitest::AssertionFailure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                        ": " + (msg)}

#define ASSERT_TRUE(cond) \
    do {                                                              \
        if (!(cond)) MINITEST_FAIL(std::string("ASSERT_TRUE(" #cond ") failed")); \
    } while (0)

#define ASSERT_EQ(a, b)                                                                        \
    do {                                                                                       \
        if (!((a) == (b))) MINITEST_FAIL(std::string("ASSERT_EQ(" #a ", " #b ") failed"));      \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                                                 \
    do {                                                                                       \
        const double _mt_a = (a), _mt_b = (b), _mt_eps = (eps);                                \
        if (std::fabs(_mt_a - _mt_b) > _mt_eps) {                                              \
            MINITEST_FAIL(std::string("ASSERT_NEAR(" #a ", " #b ") failed: ") +                \
                           std::to_string(_mt_a) + " vs " + std::to_string(_mt_b));             \
        }                                                                                       \
    } while (0)

#define ASSERT_THROWS(expr)                                                                    \
    do {                                                                                       \
        bool _mt_threw = false;                                                                \
        try {                                                                                  \
            expr;                                                                              \
        } catch (...) {                                                                        \
            _mt_threw = true;                                                                  \
        }                                                                                       \
        if (!_mt_threw) MINITEST_FAIL(std::string("ASSERT_THROWS(" #expr ") did not throw"));   \
    } while (0)

#define MINITEST_MAIN() \
    int main() { return ::minitest::runAll(); }
