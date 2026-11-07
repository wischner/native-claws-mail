//
// The session log, replacing common/log.c: protocol traces and errors
// from any thread, kept in a bounded ring for the Network Log window.
// The application decides when to look, so writing never touches a
// window and a worker thread can log freely.
//
// Derived from Claws Mail src/common/log.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include <string>
#include <string_view>
#include <vector>

namespace util {

// What a log line records.
enum class log_level { info, warning, error, sent, received };

// One logged line.
struct log_line {
    log_level level;
    std::string text;
};

// Add a line to the session log. Safe from any thread.
void log_write(log_level level, std::string_view text);

// Return a copy of the lines in order, oldest first.
std::vector<log_line> log_snapshot();

// Empty the log.
void log_clear();

} // namespace util

#endif
