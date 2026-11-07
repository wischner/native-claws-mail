//
// Base64, quoted printable and hexadecimal.
//
// Derived from Claws Mail src/common/quoted-printable.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/codec.h"

namespace util {
namespace {

constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view hex_digits = "0123456789ABCDEF";

// Return a Base64 digit's value, or -1.
int base64_value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

// Return a hexadecimal digit's value, or -1.
int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

// Append one byte as =XX.
void append_escape(std::string &out, unsigned char byte) {
    out += '=';
    out += hex_digits[byte >> 4];
    out += hex_digits[byte & 15];
}

// Encode one line, without its line end.
void qp_encode_line(std::string &out, std::string_view line) {
    std::size_t column = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto byte = static_cast<unsigned char>(line[i]);
        const bool last = i + 1 == line.size();
        const bool plain = (byte >= 33 && byte <= 126 && byte != '=') ||
                           ((byte == ' ' || byte == '\t') && !last);
        const std::size_t width = plain ? 1 : 3;
        if (column + width > 75) {
            out += "=\n";
            column = 0;
        }
        if (plain)
            out += static_cast<char>(byte);
        else
            append_escape(out, byte);
        column += width;
    }
}

} // namespace

std::string base64_encode(std::string_view data, std::size_t line_length) {
    std::string out;
    out.reserve(data.size() * 4 / 3 + 8);
    std::size_t column = 0;
    const auto emit = [&](char c) {
        out += c;
        if (line_length != 0 && ++column == line_length) {
            out += "\r\n";
            column = 0;
        }
    };
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::size_t left = data.size() - i;
        std::uint32_t chunk =
            static_cast<std::uint32_t>(static_cast<unsigned char>(data[i]))
            << 16;
        if (left > 1)
            chunk |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (left > 2)
            chunk |= static_cast<unsigned char>(data[i + 2]);
        emit(alphabet[chunk >> 18 & 63]);
        emit(alphabet[chunk >> 12 & 63]);
        emit(left > 1 ? alphabet[chunk >> 6 & 63] : '=');
        emit(left > 2 ? alphabet[chunk & 63] : '=');
    }
    if (column != 0)
        out += "\r\n";
    return out;
}

std::string base64_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 3 / 4);
    std::uint32_t chunk = 0;
    int bits = 0;
    for (const char c : text) {
        const int value = base64_value(c);
        if (value < 0)
            continue;
        chunk = chunk << 6 | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>(chunk >> bits & 255);
        }
    }
    return out;
}

std::string qp_encode(std::string_view data) {
    std::string out;
    std::size_t start = 0;
    while (start <= data.size()) {
        const std::size_t end = data.find('\n', start);
        if (end == std::string_view::npos) {
            qp_encode_line(out, data.substr(start));
            break;
        }
        qp_encode_line(out, data.substr(start, end - start));
        out += '\n';
        start = end + 1;
    }
    return out;
}

std::string qp_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '=') {
            out += text[i];
            continue;
        }
        // A soft line break, possibly after padding the sender left.
        std::size_t next = i + 1;
        while (next < text.size() &&
               (text[next] == ' ' || text[next] == '\t'))
            ++next;
        if (next < text.size() &&
            (text[next] == '\n' || text[next] == '\r')) {
            if (text[next] == '\r' && next + 1 < text.size() &&
                text[next + 1] == '\n')
                ++next;
            i = next;
            continue;
        }
        const int high = i + 1 < text.size() ? hex_value(text[i + 1]) : -1;
        const int low = i + 2 < text.size() ? hex_value(text[i + 2]) : -1;
        if (high < 0 || low < 0) {
            out += '=';
            continue;
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

std::string hex_encode(std::string_view data) {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (const char c : data) {
        const auto byte = static_cast<unsigned char>(c);
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

} // namespace util
