//
// IMAP's text forms: quoted strings and modified UTF-7 mailbox names.
//
// Derived from Claws Mail src/imap.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>

#include "net/imap_client.h"
#include "util/codec.h"
#include "util/utf8.h"

namespace net {

std::string imap_quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string imap_utf7_decode(std::string_view name) {
    std::string out;
    std::size_t i = 0;
    while (i < name.size()) {
        if (name[i] != '&') {
            out += name[i++];
            continue;
        }
        const std::size_t end = name.find('-', i);
        if (end == std::string_view::npos) {
            out += name.substr(i);
            break;
        }
        if (end == i + 1) {
            out += '&';
            i = end + 1;
            continue;
        }
        std::string encoded(name.substr(i + 1, end - i - 1));
        std::ranges::replace(encoded, ',', '/');
        const std::string bytes = util::base64_decode(encoded);
        for (std::size_t k = 0; k + 1 < bytes.size(); k += 2) {
            char32_t unit =
                static_cast<unsigned char>(bytes[k]) << 8 |
                static_cast<unsigned char>(bytes[k + 1]);
            if (unit >= 0xD800 && unit < 0xDC00 && k + 3 < bytes.size()) {
                const char32_t low =
                    static_cast<unsigned char>(bytes[k + 2]) << 8 |
                    static_cast<unsigned char>(bytes[k + 3]);
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                k += 2;
            }
            util::append_utf8(out, unit);
        }
        i = end + 1;
    }
    return out;
}

std::string imap_utf7_encode(std::string_view name) {
    const std::u32string text = util::to_utf32(name);
    std::string out;
    std::string run;
    const auto flush = [&] {
        if (run.empty())
            return;
        std::string encoded = util::base64_encode(run);
        while (encoded.ends_with('='))
            encoded.pop_back();
        std::ranges::replace(encoded, '/', ',');
        out += "&" + encoded + "-";
        run.clear();
    };
    const auto add_unit = [&](char32_t unit) {
        run += static_cast<char>(unit >> 8);
        run += static_cast<char>(unit & 255);
    };
    for (const char32_t c : text) {
        if (c >= 0x20 && c < 0x7F) {
            flush();
            if (c == '&')
                out += "&-";
            else
                out += static_cast<char>(c);
        } else if (c >= 0x10000) {
            const char32_t value = c - 0x10000;
            add_unit(0xD800 + (value >> 10));
            add_unit(0xDC00 + (value & 0x3FF));
        } else {
            add_unit(c);
        }
    }
    flush();
    return out;
}

} // namespace net
