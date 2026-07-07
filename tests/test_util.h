/*
 *  author: Claude (Anthropic)
 *
 *  Minimal, dependency-free assertion helpers for the CTest-based validation
 *  tests. Each test executable includes this header, defines test functions,
 *  runs them from `main` via `RUN(...)`, and returns `qstest::finish()`.
 *
 *  A non-zero exit code signals failure to CTest.
 * */

#ifndef QS_TEST_UTIL_h
#define QS_TEST_UTIL_h

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

namespace qstest
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inline int g_checks{0};
inline int g_failures{0};

inline void
report_failure(const char* file, int line, const std::string& msg)
{
    ++g_failures;
    std::cerr << "  [FAIL] " << file << ":" << line << ": " << msg << "\n";
}

/*
 * Prints a summary and returns the process exit code (0 = all passed).
 * */
inline int
finish()
{
    if (g_failures == 0)
    {
        std::cout << "[ PASS ] all " << g_checks << " checks passed\n";
        return 0;
    }
    std::cerr << "[ FAIL ] " << g_failures << " of " << g_checks << " checks failed\n";
    return 1;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace qstest

#define RUN(fn) do { std::cout << "[ RUN  ] " #fn "\n"; fn(); } while (0)

#define CHECK(cond) \
    do { \
        ++qstest::g_checks; \
        if (!(cond)) \
            qstest::report_failure(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        ++qstest::g_checks; \
        auto _va = (a); \
        auto _vb = (b); \
        if (!(_va == _vb)) { \
            std::ostringstream _os; \
            _os << "CHECK_EQ(" #a ", " #b ") failed: " << _va << " != " << _vb; \
            qstest::report_failure(__FILE__, __LINE__, _os.str()); \
        } \
    } while (0)

/*
 * Absolute-tolerance floating-point comparison.
 * */
#define CHECK_NEAR(a, b, eps) \
    do { \
        ++qstest::g_checks; \
        double _va = (a); \
        double _vb = (b); \
        double _eps = (eps); \
        if (std::fabs(_va - _vb) > _eps) { \
            std::ostringstream _os; \
            _os << "CHECK_NEAR(" #a ", " #b ", " #eps ") failed: |" \
                << _va << " - " << _vb << "| > " << _eps; \
            qstest::report_failure(__FILE__, __LINE__, _os.str()); \
        } \
    } while (0)

/*
 * Relative-tolerance comparison. Suited to values that span many orders of
 * magnitude (e.g. logical error rates), where an absolute epsilon is useless.
 * */
#define CHECK_REL(a, b, rel) \
    do { \
        ++qstest::g_checks; \
        double _va = (a); \
        double _vb = (b); \
        double _rel = (rel); \
        double _denom = std::fabs(_vb) > 0.0 ? std::fabs(_vb) : 1.0; \
        if (std::fabs(_va - _vb) / _denom > _rel) { \
            std::ostringstream _os; \
            _os << "CHECK_REL(" #a ", " #b ", " #rel ") failed: " \
                << _va << " vs " << _vb << " (rel err " \
                << std::fabs(_va - _vb) / _denom << " > " << _rel << ")"; \
            qstest::report_failure(__FILE__, __LINE__, _os.str()); \
        } \
    } while (0)

#endif  // QS_TEST_UTIL_h
