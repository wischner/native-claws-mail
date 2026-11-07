//
// String helpers the C sources take from glib and common/utils.c:
// trimming, ASCII case handling, splitting and numbers. Every function
// is pure, and none assumes more of its input than ASCII structure, so
// UTF-8 passes through untouched.
//
// Derived from Claws Mail src/common/utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_TEXT_H
#define UTIL_TEXT_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace util {

// Return text without leading and trailing ASCII white space.
std::string_view trim(std::string_view text);

// Return an ASCII lower case copy.
std::string lower(std::string_view text);

// Return an ASCII upper case copy.
std::string upper(std::string_view text);

// Compare ignoring ASCII case.
bool iequals(std::string_view left, std::string_view right);

// Report whether text starts with prefix, ignoring ASCII case.
bool istarts_with(std::string_view text, std::string_view prefix);

// Report whether text ends with suffix, ignoring ASCII case.
bool iends_with(std::string_view text, std::string_view suffix);

// Return the first offset of needle ignoring ASCII case, or npos.
std::size_t ifind(std::string_view text, std::string_view needle,
                  std::size_t from = 0);

// Split on a separator, keeping empty fields.
std::vector<std::string> split(std::string_view text, char separator);

//
// Split into lines.
//
// Notes:
//      LF, CRLF and bare CR all end a line. A final line ending does
//      not add an empty line.
//
std::vector<std::string> split_lines(std::string_view text);

// Join strings with a separator.
std::string join(const std::vector<std::string> &parts,
                 std::string_view separator);

// Replace every occurrence of a non-empty pattern.
std::string replace_all(std::string_view text, std::string_view pattern,
                        std::string_view replacement);

// Parse a decimal integer that fills the text, or return nothing.
std::optional<std::int64_t> parse_int(std::string_view text);

// Return the text with CR removed before each LF and bare CR made LF.
std::string normalize_newlines(std::string_view text);

// Return the text with every line ending as CRLF.
std::string to_crlf(std::string_view text);

} // namespace util

#endif
