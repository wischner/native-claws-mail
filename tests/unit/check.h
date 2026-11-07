//
// A minimal test harness: named checks, a failure count and a summary.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef TESTS_CHECK_H
#define TESTS_CHECK_H

#include <iostream>
#include <sstream>

namespace testing {

// Failures so far in this binary.
inline int failures = 0;

// Checks so far in this binary.
inline int checks = 0;

// Record one boolean check.
inline void check(bool passed, const char *text, const char *file,
                  int line) {
    ++checks;
    if (passed)
        return;
    ++failures;
    std::cerr << file << ':' << line << ": failed: " << text << '\n';
}

// Print a summary and return the process exit status.
inline int summary(const char *suite) {
    std::cout << suite << ": " << checks - failures << '/' << checks
              << " passed\n";
    return failures == 0 ? 0 : 1;
}

} // namespace testing

#define CHECK(condition) \
    testing::check((condition), #condition, __FILE__, __LINE__)

#endif
