//
// Message headers.
//
// Derived from Claws Mail src/procheader.c and src/procmime.c
// (procmime_parse_content_type),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/header.h"

#include <algorithm>

#include "mime/encoded_word.h"
#include "util/charset.h"
#include "util/text.h"

namespace mime {
namespace {

// Report whether a line starts a field.
bool is_field_line(std::string_view line) {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return false;
    return std::ranges::none_of(line.substr(0, colon), [](char c) {
        return c == ' ' || c == '\t';
    });
}

// Return a %XX-encoded RFC 2231 value decoded.
std::string percent_decode(std::string_view text) {
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() &&
            digit(text[i + 1]) >= 0 && digit(text[i + 2]) >= 0) {
            out += static_cast<char>(digit(text[i + 1]) * 16 +
                                     digit(text[i + 2]));
            i += 2;
            continue;
        }
        out += text[i];
    }
    return out;
}

// One parameter piece of an RFC 2231 continuation.
struct continuation_piece {
    int index;
    bool encoded;
    std::string text;
};

// Split at top-level semicolons, respecting quoted strings.
std::vector<std::string> split_parameters(std::string_view text) {
    std::vector<std::string> pieces(1);
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"')
            quoted = !quoted;
        if (quoted && c == '\\' && i + 1 < text.size()) {
            pieces.back() += text[++i];
            continue;
        }
        if (c == ';' && !quoted) {
            pieces.emplace_back();
            continue;
        }
        pieces.back() += c;
    }
    return pieces;
}

// Return a value without its surrounding quotes.
std::string unquote(std::string_view text) {
    text = util::trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    return std::string(text);
}

// Reassemble one parameter's continuation pieces into UTF-8.
std::string join_continuation(std::vector<continuation_piece> pieces) {
    std::ranges::sort(pieces, {}, &continuation_piece::index);
    std::string charset;
    std::string out;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        std::string_view text = pieces[i].text;
        if (i == 0 && pieces[i].encoded) {
            const std::size_t first = text.find('\'');
            const std::size_t second =
                first == std::string_view::npos
                    ? first
                    : text.find('\'', first + 1);
            if (second != std::string_view::npos) {
                charset = text.substr(0, first);
                text = text.substr(second + 1);
            }
        }
        out += pieces[i].encoded ? percent_decode(text) : std::string(text);
    }
    return util::to_utf8(out, charset);
}

} // namespace

header_block header_block::parse(std::string_view raw,
                                 std::size_t *body_offset) {
    header_block block;
    std::size_t position = 0;
    while (position < raw.size()) {
        std::size_t end = raw.find('\n', position);
        const std::size_t next =
            end == std::string_view::npos ? raw.size() : end + 1;
        if (end == std::string_view::npos)
            end = raw.size();
        std::string_view line = raw.substr(position, end - position);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty()) {
            position = next;
            break;
        }
        const bool continuation = line[0] == ' ' || line[0] == '\t';
        if (continuation && !block.fields_.empty()) {
            block.fields_.back().value += ' ';
            block.fields_.back().value += util::trim(line);
        } else if (is_field_line(line)) {
            const std::size_t colon = line.find(':');
            block.fields_.push_back(
                {std::string(line.substr(0, colon)),
                 std::string(util::trim(line.substr(colon + 1)))});
        } else {
            break;
        }
        position = next;
    }
    if (body_offset != nullptr)
        *body_offset = position;
    return block;
}

std::optional<std::string> header_block::find(std::string_view name) const {
    for (const header_field &field : fields_)
        if (util::iequals(field.name, name))
            return field.value;
    return std::nullopt;
}

std::string header_block::get(std::string_view name) const {
    return find(name).value_or("");
}

std::vector<std::string>
header_block::get_all(std::string_view name) const {
    std::vector<std::string> values;
    for (const header_field &field : fields_)
        if (util::iequals(field.name, name))
            values.push_back(field.value);
    return values;
}

void header_block::add(std::string_view name, std::string_view value) {
    fields_.push_back({std::string(name), std::string(value)});
}

void header_block::set(std::string_view name, std::string_view value) {
    for (header_field &field : fields_)
        if (util::iequals(field.name, name)) {
            field.value = value;
            return;
        }
    add(name, value);
}

void header_block::remove(std::string_view name) {
    std::erase_if(fields_, [&](const header_field &field) {
        return util::iequals(field.name, name);
    });
}

parameterized_value parse_parameters(std::string_view text) {
    parameterized_value result;
    const auto pieces = split_parameters(text);
    result.value = util::lower(util::trim(pieces.front()));
    std::map<std::string, std::vector<continuation_piece>> continued;
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        const std::size_t equal = pieces[i].find('=');
        if (equal == std::string::npos)
            continue;
        std::string name =
            util::lower(util::trim(pieces[i].substr(0, equal)));
        std::string value = unquote(pieces[i].substr(equal + 1));
        const bool encoded = !name.empty() && name.back() == '*';
        if (encoded)
            name.pop_back();
        const std::size_t star = name.find('*');
        if (star != std::string::npos) {
            const int index = static_cast<int>(
                util::parse_int(name.substr(star + 1)).value_or(0));
            continued[name.substr(0, star)].push_back(
                {index, encoded, std::move(value)});
        } else if (encoded) {
            continued[name].push_back({0, true, std::move(value)});
        } else {
            result.parameters[name] = decode_words(value);
        }
    }
    for (auto &[name, list] : continued)
        result.parameters[name] = join_continuation(std::move(list));
    return result;
}

std::string fold_header(std::string_view name, std::string_view value) {
    constexpr std::size_t limit = 78;
    std::string out(name);
    out += ':';
    std::size_t column = out.size();
    std::size_t start = 0;
    while (start < value.size()) {
        std::size_t end = value.find(' ', start + 1);
        if (end == std::string_view::npos)
            end = value.size();
        const std::string_view word = value.substr(start, end - start);
        const bool has_space = word.front() == ' ';
        if (start != 0 && column + word.size() + 1 > limit) {
            out += "\n ";
            out += util::trim(word);
            column = 1 + util::trim(word).size();
        } else {
            if (start == 0 && !has_space) {
                out += ' ';
                ++column;
            }
            out += word;
            column += word.size();
        }
        start = end;
    }
    return out;
}

} // namespace mime
