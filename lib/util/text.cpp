//
// String helpers.
//
// Derived from Claws Mail src/common/utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/text.h"

#include <algorithm>
#include <charconv>

namespace util {
namespace {

char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c;
}

char upper_ascii(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool is_white(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

} // namespace

std::string_view trim(std::string_view text) {
    while (!text.empty() && is_white(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_white(text.back()))
        text.remove_suffix(1);
    return text;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), lower_ascii);
    return out;
}

std::string upper(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), upper_ascii);
    return out;
}

bool iequals(std::string_view left, std::string_view right) {
    return left.size() == right.size() && istarts_with(left, right);
}

bool istarts_with(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower_ascii(text[i]) != lower_ascii(prefix[i]))
            return false;
    return true;
}

bool iends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           iequals(text.substr(text.size() - suffix.size()), suffix);
}

std::size_t ifind(std::string_view text, std::string_view needle,
                  std::size_t from) {
    if (needle.empty())
        return from <= text.size() ? from : std::string_view::npos;
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i)
        if (istarts_with(text.substr(i), needle))
            return i;
    return std::string_view::npos;
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r')
            continue;
        lines.emplace_back(text.substr(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            ++i;
        start = i + 1;
    }
    if (start < text.size())
        lines.emplace_back(text.substr(start));
    return lines;
}

std::string join(const std::vector<std::string> &parts,
                 std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0)
            out += separator;
        out += parts[i];
    }
    return out;
}

std::string replace_all(std::string_view text, std::string_view pattern,
                        std::string_view replacement) {
    std::string out;
    std::size_t start = 0;
    while (true) {
        const std::size_t hit = text.find(pattern, start);
        if (pattern.empty() || hit == std::string_view::npos) {
            out += text.substr(start);
            return out;
        }
        out += text.substr(start, hit - start);
        out += replacement;
        start = hit + pattern.size();
    }
}

std::optional<std::int64_t> parse_int(std::string_view text) {
    std::int64_t value = 0;
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    const char *end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || stop != end)
        return std::nullopt;
    return value;
}

std::string normalize_newlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\r') {
            out += text[i];
            continue;
        }
        out += '\n';
        if (i + 1 < text.size() && text[i + 1] == '\n')
            ++i;
    }
    return out;
}

std::string to_crlf(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 32);
    for (const char c : normalize_newlines(text)) {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

} // namespace util
