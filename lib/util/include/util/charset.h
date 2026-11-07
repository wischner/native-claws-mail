//
// Character set conversion, replacing common/codeconv.c. Mail arrives
// in every encoding ever deployed; inside Blaise all text is UTF-8, so
// conversion happens once, where a part is decoded, and once again
// where a message is written.
//
// Conversion goes through iconv where the C library has it. Without it
// UTF-8, ASCII and Latin-1 still convert, which is what keeps headless
// builds on platforms without iconv honest rather than absent.
//
// Derived from Claws Mail src/common/codeconv.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_CHARSET_H
#define UTIL_CHARSET_H

#include <optional>
#include <string>
#include <string_view>

namespace util {

//
// Convert bytes in a named charset to UTF-8.
//
// Parameters:
//      bytes       - Text in the charset.
//      charset     - MIME name; empty means assume UTF-8, then Latin-1.
//
// Returns:
//      Valid UTF-8. Undecodable bytes become U+FFFD, and an unknown
//      charset is treated as empty, so a message always displays.
//
std::string to_utf8(std::string_view bytes, std::string_view charset);

//
// Convert UTF-8 to a named charset.
//
// Returns:
//      The bytes, or nothing if the text cannot be represented exactly.
//
std::optional<std::string> from_utf8(std::string_view text,
                                     std::string_view charset);

// Report whether bytes are well-formed UTF-8.
bool is_valid_utf8(std::string_view bytes);

// Report whether every byte is seven bit.
bool is_ascii(std::string_view bytes);

} // namespace util

#endif
