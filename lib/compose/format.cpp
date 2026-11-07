//
// Quote formats, quoting and line wrapping.
//
// Derived from Claws Mail src/quote_fmt_parse.y and src/compose.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <ctime>

#include "compose/draft.h"
#include "mime/address.h"
#include "util/date.h"
#include "util/text.h"
#include "util/utf8.h"

namespace compose {
namespace {

// Return the sender's first name.
std::string first_name(std::string_view name) {
    return std::string(name.substr(0, name.find(' ')));
}

// Return the sender's last name.
std::string last_name(std::string_view name) {
    const std::size_t space = name.rfind(' ');
    return space == std::string_view::npos
               ? std::string()
               : std::string(name.substr(space + 1));
}

// Return the sender's initials.
std::string initials(std::string_view name) {
    std::string out;
    for (const std::string &word : util::split(name, ' '))
        if (!word.empty())
            out += word[0];
    return out;
}

// Return the prefix of a quoted line: its markers and their spaces.
std::string_view quote_prefix(std::string_view line) {
    std::size_t at = 0;
    std::size_t end = 0;
    while (at < line.size()) {
        const char c = line[at];
        if (c == '>' || c == '|') {
            end = ++at;
            if (at < line.size() && line[at] == ' ')
                end = ++at;
        } else if (c == ' ' && at == end) {
            ++at;
        } else {
            break;
        }
    }
    return line.substr(0, end);
}

// Return the number of code points.
std::size_t length_of(std::string_view text) {
    return util::to_utf32(text).size();
}

// Wrap one logical line under a prefix.
void wrap_line(std::string &out, std::string_view line, int width) {
    const std::string prefix(quote_prefix(line));
    const auto limit = static_cast<std::size_t>(std::max(width, 20));
    std::string_view rest = line.substr(prefix.size());
    std::string current = prefix;
    std::size_t column = length_of(prefix);
    bool has_word = false;
    for (const std::string &word : util::split(rest, ' ')) {
        const std::size_t size = length_of(word);
        if (has_word && column + 1 + size > limit) {
            out += current + '\n';
            current = prefix;
            column = length_of(prefix);
            has_word = false;
        }
        if (has_word) {
            current += ' ';
            ++column;
        }
        current += word;
        column += size;
        has_word = true;
    }
    out += current;
}

} // namespace

std::string quote_text(std::string_view text, std::string_view mark) {
    std::string out;
    for (const std::string &line : util::split_lines(text)) {
        // A mark on an already quoted line nests without a gap.
        if (line.starts_with('>') && mark.ends_with(' ') &&
            mark.substr(0, mark.size() - 1) == ">")
            out += ">";
        else
            out += mark;
        out += line;
        out += '\n';
    }
    return out;
}

std::string strip_signature(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line = text.substr(
            start, end == std::string_view::npos ? text.size() - start
                                                 : end - start);
        if (line == "-- " || line == "--")
            return std::string(text.substr(0, start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return std::string(text);
}

std::string expand_format(std::string_view format,
                          const mail::msg_info &info, std::string_view body,
                          std::string_view quote_mark,
                          const prefs::account &account,
                          std::size_t *cursor) {
    std::string out;
    const mime::mailbox sender = mime::parse_address(info.from);
    for (std::size_t i = 0; i < format.size(); ++i) {
        const char c = format[i];
        if (c == '\\' && i + 1 < format.size()) {
            const char next = format[++i];
            out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            continue;
        }
        if (c != '%' || i + 1 == format.size()) {
            out += c;
            continue;
        }
        const char code = format[++i];
        switch (code) {
        case 'd':
            out += info.date;
            break;
        case 'f':
            out += info.from;
            break;
        case 'A':
            out += sender.address;
            break;
        case 'N':
            out += info.from_name;
            break;
        case 'F':
            out += first_name(info.from_name);
            break;
        case 'L':
            out += last_name(info.from_name);
            break;
        case 'I':
            out += initials(info.from_name);
            break;
        case 's':
            out += info.subject;
            break;
        case 't':
            out += info.to;
            break;
        case 'c':
            out += info.cc;
            break;
        case 'n':
            out += info.newsgroups;
            break;
        case 'i':
            out += info.message_id;
            break;
        case 'r':
            out += util::join(info.references, " ");
            break;
        case 'M':
            out += body;
            break;
        case 'm':
            out += strip_signature(body);
            break;
        case 'Q':
            out += quote_text(body, quote_mark);
            break;
        case 'q':
            out += quote_text(strip_signature(body), quote_mark);
            break;
        case 'X':
            if (cursor != nullptr)
                *cursor = out.size();
            break;
        case 'a':
            if (i + 1 < format.size()) {
                const char field = format[++i];
                if (field == 'f')
                    out += account.full_name;
                else if (field == 'm')
                    out += account.address;
                else if (field == 'n')
                    out += account.account_name;
                else if (field == 'o')
                    out += account.organization;
                else if (field == 's')
                    out += account.signature_text;
            }
            break;
        case '%':
            out += '%';
            break;
        default:
            out += '%';
            out += code;
            break;
        }
    }
    return out;
}

std::string wrap_text(std::string_view text, int width) {
    std::string out;
    const bool final_newline = text.ends_with('\n');
    const auto lines = util::split_lines(text);
    bool in_signature = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string &line = lines[i];
        in_signature |= line == "-- ";
        if (i != 0)
            out += '\n';
        if (in_signature ||
            length_of(line) <= static_cast<std::size_t>(width)) {
            out += line;
            continue;
        }
        wrap_line(out, line, width);
    }
    if (final_newline)
        out += '\n';
    return out;
}

} // namespace compose
