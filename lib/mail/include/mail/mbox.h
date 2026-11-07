//
// The mbox format: importing and exporting folders, and reading a
// system spool. Replaces src/mbox.c.
//
// Derived from Claws Mail src/mbox.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_MBOX_H
#define MAIL_MBOX_H

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace mail {

//
// Split an mbox file into messages.
//
// Notes:
//      Messages are delimited by "From " lines after an empty line, and
//      ">From " lines inside them are unescaped.
//
std::vector<std::string> split_mbox(std::string_view text);

// Append a message to mbox text.
void append_mbox(std::string &out, std::string_view message,
                 std::string_view sender, std::time_t when);

} // namespace mail

#endif
