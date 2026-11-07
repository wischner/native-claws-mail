//
// The binary-to-text codecs and digests mail needs: Base64, quoted
// printable, MD5 and HMAC-MD5. MD5 is obsolete for anything new; it is
// here because APOP and CRAM-MD5 are defined in terms of it.
//
// Derived from Claws Mail src/common/quoted-printable.c, md5.c and
// base64 use in src/procmime.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_CODEC_H
#define UTIL_CODEC_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace util {

//
// Encode Base64.
//
// Parameters:
//      data        - Bytes to encode.
//      line_length - Characters per CRLF-ended line, or 0 for one line.
//
std::string base64_encode(std::string_view data, std::size_t line_length = 0);

// Decode Base64, skipping anything outside the alphabet.
std::string base64_decode(std::string_view text);

//
// Encode quoted printable (RFC 2045).
//
// Parameters:
//      data        - Bytes with LF line ends.
//
// Returns:
//      Encoded text, soft-wrapped at 76 columns, with LF line ends.
//
std::string qp_encode(std::string_view data);

// Decode quoted printable, leaving malformed escapes literal.
std::string qp_decode(std::string_view text);

// Return lower case hexadecimal.
std::string hex_encode(std::string_view data);

// Return the 16 byte MD5 digest of data.
std::array<std::uint8_t, 16> md5(std::string_view data);

// Return the MD5 digest of data as hexadecimal.
std::string md5_hex(std::string_view data);

// Return HMAC-MD5 of data as hexadecimal, as CRAM-MD5 sends it.
std::string hmac_md5_hex(std::string_view key, std::string_view data);

} // namespace util

#endif
