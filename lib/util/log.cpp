//
// The session log.
//
// Derived from Claws Mail src/common/log.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/log.h"

#include <deque>
#include <mutex>

namespace util {
namespace {

constexpr std::size_t capacity = 2000;

std::mutex lines_lock;
std::deque<log_line> lines;

} // namespace

void log_write(log_level level, std::string_view text) {
    const std::scoped_lock hold(lines_lock);
    lines.push_back({level, std::string(text)});
    if (lines.size() > capacity)
        lines.pop_front();
}

std::vector<log_line> log_snapshot() {
    const std::scoped_lock hold(lines_lock);
    return {lines.begin(), lines.end()};
}

void log_clear() {
    const std::scoped_lock hold(lines_lock);
    lines.clear();
}

} // namespace util
