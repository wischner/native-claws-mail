//
// Hand-rolled UTF-8 transcoding, because the standard library's
// codecvt facilities are deprecated and the rules are short.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/utf8.h"

#include <cwctype>

namespace util {
namespace {

constexpr char32_t replacement = 0xfffd;

// Return the length of the sequence a lead byte announces, or 0.
int sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xe0) == 0xc0) return 2;
    if ((lead & 0xf0) == 0xe0) return 3;
    if ((lead & 0xf8) == 0xf0) return 4;
    return 0;
}

} // namespace

std::u32string to_utf32(std::string_view utf8) {
    std::u32string result;
    result.reserve(utf8.size());
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[index]);
        const int length = sequence_length(lead);
        if (length == 0 || index + length > utf8.size()) {
            result.push_back(replacement);
            ++index;
            continue;
        }
        if (length == 1) {
            result.push_back(lead);
            ++index;
            continue;
        }
        char32_t value = lead & (0xff >> (length + 1));
        bool valid = true;
        for (int part = 1; part < length; ++part) {
            const auto next =
                static_cast<unsigned char>(utf8[index + part]);
            if ((next & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            value = (value << 6) | (next & 0x3f);
        }
        if (!valid) {
            result.push_back(replacement);
            ++index;
            continue;
        }
        result.push_back(value);
        index += length;
    }
    return result;
}

void append_utf8(std::string &out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
    }
}

std::string to_utf8(std::u32string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char32_t c : text)
        append_utf8(result, c);
    return result;
}

bool is_space(char32_t c) {
    return c == U' ' || c == U'\t' || c == 0x3000 ||
           (c >= 0x2000 && c <= 0x200a);
}

bool is_word_character(char32_t c) {
    if (c < 0x80) {
        return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') ||
               (c >= U'A' && c <= U'Z') || c == U'_';
    }
    return !is_space(c) && !(c >= 0x2010 && c <= 0x206f) && c != 0xa0;
}

char32_t fold_case(char32_t c) {
    if (c >= U'A' && c <= U'Z')
        return c + 32;
    if (c < 0x80)
        return c;
    return static_cast<char32_t>(
        std::towlower(static_cast<std::wint_t>(c)));
}

} // namespace util
