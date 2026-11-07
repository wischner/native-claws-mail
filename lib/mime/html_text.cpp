//
// HTML and text/enriched to plain text.
//
// Derived from Claws Mail src/html.c and src/enriched.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/html_text.h"

#include <array>
#include <cstdint>

#include "util/text.h"
#include "util/utf8.h"

namespace mime {
namespace {

// A named character reference.
struct entity {
    const char *name;
    char32_t value;
};

constexpr std::array<entity, 29> entities{{
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},
    {"quot", '"'},      {"apos", '\''},     {"nbsp", 0xA0},
    {"copy", 0xA9},     {"reg", 0xAE},      {"trade", 0x2122},
    {"hellip", 0x2026}, {"mdash", 0x2014},  {"ndash", 0x2013},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},
    {"rdquo", 0x201D},  {"bull", 0x2022},   {"middot", 0xB7},
    {"laquo", 0xAB},    {"raquo", 0xBB},    {"euro", 0x20AC},
    {"pound", 0xA3},    {"yen", 0xA5},      {"cent", 0xA2},
    {"deg", 0xB0},      {"times", 0xD7},    {"auml", 0xE4},
    {"ouml", 0xF6},     {"uuml", 0xFC},
}};

// Converts one document.
class html_converter {
public:
    explicit html_converter(std::string_view html) : html_(html) {}

    // Run the conversion.
    std::string run() {
        while (position_ < html_.size()) {
            const char c = html_[position_];
            if (c == '<')
                read_tag();
            else if (c == '&')
                read_entity();
            else
                read_character(c);
        }
        return finish();
    }

private:
    // Add text with HTML's collapsing of white space.
    void read_character(char c) {
        ++position_;
        if (skipping_)
            return;
        if (preformatted_) {
            out_ += c;
            return;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending_space_ = !out_.empty() && out_.back() != '\n';
            return;
        }
        flush_space();
        out_ += c;
    }

    void flush_space() {
        if (pending_space_ && !out_.empty() && out_.back() != '\n')
            out_ += ' ';
        pending_space_ = false;
    }

    // Read a reference at the current ampersand.
    void read_entity() {
        const std::size_t end = html_.find(';', position_);
        if (end == std::string_view::npos || end - position_ > 10) {
            read_character('&');
            return;
        }
        const std::string_view name =
            html_.substr(position_ + 1, end - position_ - 1);
        const char32_t value = resolve(name);
        if (value == 0) {
            read_character('&');
            return;
        }
        position_ = end + 1;
        if (skipping_)
            return;
        flush_space();
        util::append_utf8(out_, value == 0xA0 ? U' ' : value);
    }

    // Return a reference's character, or zero.
    static char32_t resolve(std::string_view name) {
        if (!name.empty() && name[0] == '#') {
            const bool hex = name.size() > 1 &&
                             (name[1] == 'x' || name[1] == 'X');
            std::int64_t code = 0;
            for (const char c : name.substr(hex ? 2 : 1)) {
                const int digit =
                    c >= '0' && c <= '9' ? c - '0'
                    : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                  : -1;
                if (digit < 0 || code > 0x10FFFF)
                    return 0;
                code = code * (hex ? 16 : 10) + digit;
            }
            return code > 0 && code <= 0x10FFFF
                       ? static_cast<char32_t>(code)
                       : 0;
        }
        for (const entity &item : entities)
            if (name == item.name)
                return item.value;
        return 0;
    }

    // Read one tag at the current angle bracket.
    void read_tag() {
        if (html_.substr(position_, 4) == "<!--") {
            const std::size_t end = html_.find("-->", position_);
            position_ = end == std::string_view::npos ? html_.size()
                                                      : end + 3;
            return;
        }
        std::size_t end = position_ + 1;
        char quote = 0;
        while (end < html_.size() && (html_[end] != '>' || quote != 0)) {
            if (quote != 0 && html_[end] == quote)
                quote = 0;
            else if (quote == 0 &&
                     (html_[end] == '"' || html_[end] == '\''))
                quote = html_[end];
            ++end;
        }
        const std::string_view tag =
            html_.substr(position_ + 1, end - position_ - 1);
        position_ = end < html_.size() ? end + 1 : end;
        handle_tag(tag);
    }

    // Return the value of a tag's attribute, or empty.
    static std::string attribute(std::string_view tag,
                                 std::string_view name) {
        std::size_t at = 0;
        while ((at = util::ifind(tag, name, at)) !=
               std::string_view::npos) {
            std::size_t cursor = at + name.size();
            while (cursor < tag.size() && tag[cursor] == ' ')
                ++cursor;
            if (cursor < tag.size() && tag[cursor] == '=') {
                ++cursor;
                while (cursor < tag.size() && tag[cursor] == ' ')
                    ++cursor;
                const char quote =
                    cursor < tag.size() ? tag[cursor] : '\0';
                if (quote == '"' || quote == '\'') {
                    const std::size_t end = tag.find(quote, cursor + 1);
                    return std::string(tag.substr(
                        cursor + 1, end == std::string_view::npos
                                        ? std::string_view::npos
                                        : end - cursor - 1));
                }
                std::size_t end = cursor;
                while (end < tag.size() && tag[end] != ' ')
                    ++end;
                return std::string(tag.substr(cursor, end - cursor));
            }
            at = cursor;
        }
        return {};
    }

    // End the current line.
    void break_line() {
        pending_space_ = false;
        if (!out_.empty() && out_.back() != '\n')
            out_ += '\n';
    }

    // Leave one empty line.
    void break_paragraph() {
        break_line();
        if (out_.size() >= 2 && !out_.ends_with("\n\n"))
            out_ += '\n';
    }

    // React to a tag.
    void handle_tag(std::string_view tag) {
        const bool closing = tag.starts_with('/');
        const std::string_view body = closing ? tag.substr(1) : tag;
        std::size_t stop = 0;
        while (stop < body.size() && body[stop] != ' ' &&
               body[stop] != '/' && body[stop] != '\n' &&
               body[stop] != '\t')
            ++stop;
        const std::string name = util::lower(body.substr(0, stop));

        if (name == "script" || name == "style" || name == "head" ||
            name == "title") {
            skipping_ = !closing;
            return;
        }
        if (skipping_)
            return;
        if (name == "br") {
            break_line();
            if (out_.empty() || !out_.ends_with('\n'))
                out_ += '\n';
        } else if (name == "p" || name == "div" || name == "table" ||
                   name == "tr" || name == "blockquote" ||
                   name == "ul" || name == "ol" || name == "section" ||
                   name == "article") {
            if (name == "div" || name == "tr")
                break_line();
            else
                break_paragraph();
        } else if (name.size() == 2 && name[0] == 'h' &&
                   name[1] >= '1' && name[1] <= '6') {
            break_paragraph();
        } else if (name == "hr") {
            break_line();
            out_ += "------------------------------\n";
        } else if (name == "li" && !closing) {
            break_line();
            util::append_utf8(out_, U'•');
            out_ += ' ';
        } else if (name == "td" || name == "th") {
            if (closing)
                out_ += '\t';
        } else if (name == "pre") {
            preformatted_ = !closing;
            break_line();
        } else if (name == "a") {
            if (!closing) {
                link_ = attribute(tag, "href");
                link_start_ = out_.size();
            } else {
                append_link();
            }
        } else if (name == "img" && !closing) {
            const std::string alt = attribute(tag, "alt");
            if (!alt.empty()) {
                flush_space();
                out_ += "[" + alt + "]";
            }
        }
    }

    // Show a link target after its text, unless the text says it already.
    void append_link() {
        if (link_.empty() || link_.starts_with('#') ||
            link_.starts_with("javascript:"))
            return;
        const std::string_view text =
            std::string_view(out_).substr(link_start_);
        if (text.find(link_) == std::string_view::npos)
            out_ += " <" + link_ + ">";
        link_.clear();
    }

    // Trim the result and collapse runs of empty lines.
    std::string finish() const {
        std::string result;
        int empties = 0;
        for (const std::string &raw : util::split_lines(out_)) {
            std::string_view line = raw;
            while (!line.empty() &&
                   (line.back() == ' ' || line.back() == '\t'))
                line.remove_suffix(1);
            if (line.empty()) {
                if (++empties > 1 || result.empty())
                    continue;
            } else {
                empties = 0;
            }
            result += line;
            result += '\n';
        }
        while (result.ends_with("\n\n"))
            result.pop_back();
        return result;
    }

    std::string_view html_;
    std::size_t position_ = 0;
    std::string out_;
    std::string link_;
    std::size_t link_start_ = 0;
    bool pending_space_ = false;
    bool skipping_ = false;
    bool preformatted_ = false;
};

} // namespace

std::string html_to_text(std::string_view html) {
    return html_converter(html).run();
}

std::string enriched_to_text(std::string_view enriched) {
    std::string out;
    std::size_t i = 0;
    while (i < enriched.size()) {
        const char c = enriched[i];
        if (c == '<') {
            if (enriched.substr(i, 2) == "<<") {
                out += '<';
                i += 2;
                continue;
            }
            const std::size_t end = enriched.find('>', i);
            if (end == std::string_view::npos)
                break;
            const std::string_view tag = enriched.substr(i + 1, end - i - 1);
            i = end + 1;
            if (util::iequals(tag, "param")) {
                const std::size_t close =
                    util::ifind(enriched, "</param>", i);
                i = close == std::string_view::npos ? enriched.size()
                                                    : close + 8;
            }
            continue;
        }
        if (c == '\n') {
            // One newline is a space; n newlines are n-1 line breaks.
            std::size_t run = 0;
            while (i < enriched.size() && enriched[i] == '\n') {
                ++run;
                ++i;
            }
            if (run == 1)
                out += ' ';
            else
                out.append(run - 1, '\n');
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

} // namespace mime
