#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace xwine::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& current_failures() {
    static int fails = 0;
    return fails;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void report_failure(const char* file, int line, const std::string& msg) {
    ++current_failures();
    std::cerr << "  [FAIL] " << file << ":" << line << " — " << msg << "\n";
}

inline int run_all_tests(const char* suite_name) {
    int passed = 0;
    int failed = 0;
    std::cout << "=== Running Suite: " << suite_name << " (" << registry().size() << " tests) ===\n";
    for (const auto& tc : registry()) {
        current_failures() = 0;
        try {
            tc.fn();
        } catch (const std::exception& ex) {
            report_failure(__FILE__, __LINE__, std::string("Unhandled exception: ") + ex.what());
        } catch (...) {
            report_failure(__FILE__, __LINE__, "Unhandled unknown exception");
        }
        if (current_failures() == 0) {
            std::cout << "  [PASS] " << tc.name << "\n";
            ++passed;
        } else {
            std::cerr << "  [FAIL] " << tc.name << " (" << current_failures() << " assertion(s) failed)\n";
            ++failed;
        }
    }
    std::cout << "=== Suite " << suite_name << ": " << passed << " passed, " << failed << " failed ===\n";
    return failed == 0 ? 0 : 1;
}

} // namespace xwine::test

#define XWINE_TEST(TestName)                                                      \
    static void test_fn_##TestName();                                             \
    static ::xwine::test::Registrar reg_##TestName(#TestName, test_fn_##TestName); \
    static void test_fn_##TestName()

#define XWINE_EXPECT_TRUE(cond)                                                   \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ::xwine::test::report_failure(__FILE__, __LINE__, "Expected true: " #cond); \
        }                                                                         \
    } while (0)

#define XWINE_EXPECT_FALSE(cond)                                                  \
    do {                                                                          \
        if (cond) {                                                               \
            ::xwine::test::report_failure(__FILE__, __LINE__, "Expected false: " #cond); \
        }                                                                         \
    } while (0)

#define XWINE_EXPECT_EQ(a, b)                                                     \
    do {                                                                          \
        const auto _va = (a);                                                     \
        const auto _vb = (b);                                                     \
        if (!(_va == _vb)) {                                                      \
            std::ostringstream _oss;                                              \
            _oss << "Expected " #a " == " #b " (got " << _va << " vs " << _vb << ")"; \
            ::xwine::test::report_failure(__FILE__, __LINE__, _oss.str());        \
        }                                                                         \
    } while (0)

#define XWINE_EXPECT_NEAR(a, b, eps)                                              \
    do {                                                                          \
        const double _va = static_cast<double>(a);                                \
        const double _vb = static_cast<double>(b);                                \
        if (std::fabs(_va - _vb) > static_cast<double>(eps)) {                    \
            std::ostringstream _oss;                                              \
            _oss << "Expected " #a " ~= " #b " (got " << _va << " vs " << _vb << ")"; \
            ::xwine::test::report_failure(__FILE__, __LINE__, _oss.str());        \
        }                                                                         \
    } while (0)
