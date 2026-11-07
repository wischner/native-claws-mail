//
// RFC 2047 encoded words, in both directions. Replaces unmime_header
// and conv_encode_header.
//
// Derived from Claws Mail src/common/unmime.c and
// src/common/codeconv.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_ENCODED_WORD_H
#define MIME_ENCODED_WORD_H

#include <string>
#include <string_view>

namespace mime {

//
// Decode a header value to UTF-8.
//
// Parameters:
//      text        - An unfolded header value.
//
// Notes:
//      White space between adjacent encoded words is dropped, as the RFC
//      requires. Raw eight-bit text is assumed UTF-8, then Latin-1, so
//      the unlabelled mail senders produce is still readable.
//
std::string decode_words(std::string_view text);

//
// Encode a header value when it needs it.
//
// Parameters:
//      text        - UTF-8 header value.
//
// Returns:
//      The value unchanged when it is ASCII, otherwise the non-ASCII
//      runs as UTF-8 Q or B encoded words no longer than 75 columns.
//
std::string encode_words(std::string_view text);

// Encode only the display names in an address list.
std::string encode_address_list(std::string_view text);

} // namespace mime

#endif
