//
// Character set conversion through iconv, with built-in fallbacks.
//
// Derived from Claws Mail src/common/codeconv.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/charset.h"

#include <algorithm>

#if __has_include(<iconv.h>)
#define BLAISE_HAS_ICONV 1
#include <iconv.h>
#endif

#include "util/text.h"
#include "util/utf8.h"

namespace util {
namespace {

// Report whether the name means UTF-8.
bool is_utf8_name(std::string_view charset) {
    return iequals(charset, "utf-8") || iequals(charset, "utf8");
}

// Report whether the name means ASCII.
bool is_ascii_name(std::string_view charset) {
    return iequals(charset, "us-ascii") || iequals(charset, "ascii");
}

// Report whether the name means ISO 8859-1.
bool is_latin1_name(std::string_view charset) {
    return iequals(charset, "iso-8859-1") || iequals(charset, "latin1");
}

// Return Latin-1 bytes as UTF-8.
std::string latin1_to_utf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 8);
    for (const char c : bytes)
        append_utf8(out, static_cast<unsigned char>(c));
    return out;
}

// Return UTF-8 with damage replaced by U+FFFD.
std::string repair_utf8(std::string_view bytes) {
    return to_utf8(to_utf32(bytes));
}

#ifdef BLAISE_HAS_ICONV

// Convert through iconv; returns nothing if the charset is unknown.
// With strict set, an unconvertible input also fails; otherwise each
// bad byte is replaced.
std::optional<std::string> run_iconv(std::string_view bytes,
                                     const std::string &from,
                                     const std::string &to, bool strict) {
    const iconv_t handle = iconv_open(to.c_str(), from.c_str());
    if (handle == reinterpret_cast<iconv_t>(-1))
        return std::nullopt;
    std::string out;
    char buffer[4096];
    char *in = const_cast<char *>(bytes.data());
    std::size_t in_left = bytes.size();
    bool flushed = false;
    while (!flushed) {
        char *next = buffer;
        std::size_t room = sizeof buffer;
        const std::size_t result =
            in_left != 0 ? iconv(handle, &in, &in_left, &next, &room)
                         : iconv(handle, nullptr, nullptr, &next, &room);
        const bool stuck =
            result == static_cast<std::size_t>(-1) && next == buffer &&
            room == sizeof buffer;
        out.append(buffer, static_cast<std::size_t>(next - buffer));
        if (in_left == 0) {
            flushed = result != static_cast<std::size_t>(-1);
            if (!flushed && stuck)
                flushed = true;
            continue;
        }
        if (result != static_cast<std::size_t>(-1))
            continue;
        if (room >= 16 || stuck) {
            if (strict) {
                iconv_close(handle);
                return std::nullopt;
            }
            out += "\xEF\xBF\xBD";
            ++in;
            --in_left;
        }
    }
    iconv_close(handle);
    return out;
}

#endif

} // namespace

bool is_ascii(std::string_view bytes) {
    return std::ranges::all_of(
        bytes, [](char c) { return static_cast<unsigned char>(c) < 128; });
}

bool is_valid_utf8(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        std::size_t length = 1;
        char32_t code = lead;
        if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            code = lead & 7;
        } else if (lead >= 0xE0 && lead < 0xF0) {
            length = 3;
            code = lead & 15;
        } else if (lead >= 0xC2 && lead < 0xE0) {
            length = 2;
            code = lead & 31;
        } else if (lead >= 0x80) {
            return false;
        }
        if (i + length > bytes.size())
            return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(bytes[i + k]);
            if ((next & 0xC0) != 0x80)
                return false;
            code = code << 6 | (next & 63);
        }
        const bool overlong = (length == 3 && code < 0x800) ||
                              (length == 4 && code < 0x10000);
        if (overlong || code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF))
            return false;
        i += length;
    }
    return true;
}

std::string to_utf8(std::string_view bytes, std::string_view charset) {
    if (is_ascii(bytes))
        return std::string(bytes);
    if (is_utf8_name(charset))
        return repair_utf8(bytes);
    if (is_latin1_name(charset))
        return latin1_to_utf8(bytes);
#ifdef BLAISE_HAS_ICONV
    if (!charset.empty() && !is_ascii_name(charset)) {
        // Mail labelled ISO-8859-1 is often really Windows-1252, but the
        // label is what the sender declared, so it is honored as written.
        if (auto converted = run_iconv(bytes, std::string(charset), "UTF-8",
                                       false))
            return repair_utf8(*converted);
    }
#endif
    return is_valid_utf8(bytes) ? std::string(bytes) : latin1_to_utf8(bytes);
}

std::optional<std::string> from_utf8(std::string_view text,
                                     std::string_view charset) {
    if (is_utf8_name(charset))
        return std::string(text);
    if (is_ascii(text))
        return std::string(text);
    if (is_ascii_name(charset))
        return std::nullopt;
    if (is_latin1_name(charset)) {
        std::string out;
        for (const char32_t c : to_utf32(text)) {
            if (c > 0xFF)
                return std::nullopt;
            out += static_cast<char>(c);
        }
        return out;
    }
#ifdef BLAISE_HAS_ICONV
    return run_iconv(text, "UTF-8", std::string(charset), true);
#else
    return std::nullopt;
#endif
}

} // namespace util
