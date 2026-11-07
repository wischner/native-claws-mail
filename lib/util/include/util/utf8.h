//
// Conversions between UTF-8, the file and clipboard encoding, and
// UTF-32, used where text is edited or measured by character.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_UTF8_H
#define UTIL_UTF8_H

#include <string>
#include <string_view>

namespace util {

//
// Decode UTF-8 to code points.
//
// Notes:
//      A malformed sequence decodes to U+FFFD and decoding resumes at
//      the next byte, so a damaged file still loads.
//
std::u32string to_utf32(std::string_view utf8);

// Encode code points as UTF-8.
std::string to_utf8(std::u32string_view text);

// Append one code point to a UTF-8 string.
void append_utf8(std::string &out, char32_t character);

// Report whether a code point is a space a line can break after.
bool is_space(char32_t character);

// Report whether a code point belongs to a word for selection.
bool is_word_character(char32_t character);

// Return a code point with simple case folding for search.
char32_t fold_case(char32_t character);

} // namespace util

#endif
