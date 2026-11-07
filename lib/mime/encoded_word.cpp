//
// RFC 2047 encoded words.
//
// Derived from Claws Mail src/common/unmime.c and
// src/common/codeconv.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/encoded_word.h"

#include <vector>

#include "mime/address.h"
#include "util/charset.h"
#include "util/codec.h"
#include "util/text.h"

namespace mime {
namespace {

// One word recognized in a header.
struct encoded_word {
    std::size_t end = 0;
    std::string charset;
    std::string bytes;
};

// Decode the Q form, where underscore means space.
std::string decode_q(std::string_view text) {
    std::string spaced(text);
    for (char &c : spaced)
        if (c == '_')
            c = ' ';
    return util::qp_decode(spaced);
}

// Recognize an encoded word at an offset.
bool read_word(std::string_view text, std::size_t start, encoded_word &word) {
    if (text.substr(start, 2) != "=?")
        return false;
    const std::size_t charset_end = text.find('?', start + 2);
    if (charset_end == std::string_view::npos ||
        charset_end + 2 >= text.size() || text[charset_end + 2] != '?')
        return false;
    const char method = text[charset_end + 1];
    const std::size_t payload = charset_end + 3;
    const std::size_t end = text.find("?=", payload);
    if (end == std::string_view::npos)
        return false;
    const std::string_view data = text.substr(payload, end - payload);
    if (method == 'b' || method == 'B')
        word.bytes = util::base64_decode(data);
    else if (method == 'q' || method == 'Q')
        word.bytes = decode_q(data);
    else
        return false;
    word.charset = text.substr(start + 2, charset_end - start - 2);
    // A language tag may follow an asterisk in the charset (RFC 2231).
    if (const std::size_t star = word.charset.find('*');
        star != std::string::npos)
        word.charset.resize(star);
    word.end = end + 2;
    return true;
}

// Return a word Q encoded, or empty when it would not fit a word.
std::string encode_q(std::string_view text) {
    static constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out = "=?UTF-8?Q?";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == ' ') {
            out += '_';
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '-') {
            out += c;
        } else {
            out += '=';
            out += digits[byte >> 4];
            out += digits[byte & 15];
        }
    }
    return out + "?=";
}

// Return a UTF-8 chunk boundary at or below limit.
std::size_t chunk_end(std::string_view text, std::size_t limit) {
    if (limit >= text.size())
        return text.size();
    while (limit > 0 &&
           (static_cast<unsigned char>(text[limit]) & 0xC0) == 0x80)
        --limit;
    return limit;
}

} // namespace

std::string decode_words(std::string_view text) {
    std::string out;
    std::string pending; // Raw text not yet known to be UTF-8.
    std::string run;     // Decoded bytes of a same-charset word run.
    std::string run_charset;
    const auto flush_run = [&] {
        if (!run.empty())
            out += util::to_utf8(run, run_charset);
        run.clear();
    };
    const auto flush_pending = [&] {
        if (!pending.empty())
            out += util::to_utf8(pending, "");
        pending.clear();
    };
    std::size_t position = 0;
    bool after_word = false;
    while (position < text.size()) {
        encoded_word word;
        if (read_word(text, position, word)) {
            // Space between two encoded words is not text.
            if (after_word && !pending.empty() &&
                util::trim(pending).empty())
                pending.clear();
            flush_pending();
            if (!util::iequals(word.charset, run_charset))
                flush_run();
            run_charset = word.charset;
            // Concatenate: a multibyte character may span two words.
            run += word.bytes;
            position = word.end;
            after_word = true;
            continue;
        }
        const bool space = text[position] == ' ' || text[position] == '\t';
        if (!space || !after_word)
            after_word = after_word && space;
        if (!space)
            flush_run();
        pending += text[position++];
        if (!space && !pending.empty() && run.empty())
            after_word = false;
    }
    flush_run();
    flush_pending();
    return out;
}

std::string encode_words(std::string_view text) {
    if (util::is_ascii(text))
        return std::string(text);
    constexpr std::size_t chunk = 20; // Bytes; Q at most triples them.
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end =
            start + chunk_end(text.substr(start), chunk);
        words.push_back(encode_q(text.substr(start, end - start)));
        start = end;
    }
    return util::join(words, " ");
}

std::string encode_address_list(std::string_view text) {
    if (util::is_ascii(text))
        return std::string(text);
    std::vector<mailbox> boxes = parse_addresses(text);
    std::vector<std::string> parts;
    for (mailbox &box : boxes) {
        if (util::is_ascii(box.name)) {
            parts.push_back(format_mailbox(box));
            continue;
        }
        parts.push_back(encode_words(box.name) + " <" + box.address + ">");
    }
    return util::join(parts, ", ");
}

} // namespace mime
