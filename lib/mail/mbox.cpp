//
// The mbox format.
//
// Derived from Claws Mail src/mbox.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/mbox.h"

#include "util/date.h"
#include "util/text.h"

namespace mail {
namespace {

// Report whether a line is an escaped From, any number of > deep.
bool is_escaped_from(std::string_view line) {
    while (line.starts_with('>'))
        line.remove_prefix(1);
    return line.starts_with("From ");
}

} // namespace

std::vector<std::string> split_mbox(std::string_view text) {
    std::vector<std::string> messages;
    std::string current;
    bool in_message = false;
    bool previous_empty = true;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        const std::size_t next =
            end == std::string_view::npos ? text.size() : end + 1;
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        if (previous_empty && line.starts_with("From ")) {
            if (in_message) {
                if (current.ends_with("\n\n"))
                    current.pop_back();
                messages.push_back(std::move(current));
            }
            current.clear();
            in_message = true;
        } else if (in_message) {
            if (line.starts_with('>') && is_escaped_from(line))
                line.remove_prefix(1);
            current += line;
            current += '\n';
        }
        previous_empty = line.empty();
        start = next;
    }
    if (in_message) {
        if (current.ends_with("\n\n"))
            current.pop_back();
        messages.push_back(std::move(current));
    }
    return messages;
}

void append_mbox(std::string &out, std::string_view message,
                 std::string_view sender, std::time_t when) {
    const std::tm fields = util::utc_fields(when);
    char stamp[64];
    std::strftime(stamp, sizeof stamp, "%a %b %e %H:%M:%S %Y", &fields);
    out += "From ";
    out += sender.empty() ? std::string_view("MAILER-DAEMON") : sender;
    out += ' ';
    out += stamp;
    out += '\n';
    for (const std::string &line :
         util::split_lines(util::normalize_newlines(message))) {
        if (is_escaped_from(line))
            out += '>';
        out += line;
        out += '\n';
    }
    out += '\n';
}

} // namespace mail
