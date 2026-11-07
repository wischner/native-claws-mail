//
// A recursive-free XML scanner. Element nesting is verified with an
// explicit stack, so a deeply nested hostile file cannot overflow the
// C++ stack.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/xml_reader.h"

#include <cctype>
#include <cstdlib>
#include <vector>

#include "util/utf8.h"

namespace util {
namespace {

// Report whether a character can continue a name.
bool is_name_character(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == ':' ||
           c == '_' || c == '-' || c == '.' ||
           (static_cast<unsigned char>(c) & 0x80);
}

// Report XML whitespace.
bool is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Resolve entity references in raw text.
std::string decoded(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    std::size_t index = 0;
    while (index < raw.size()) {
        const char c = raw[index];
        if (c != '&') {
            result.push_back(c);
            ++index;
            continue;
        }
        const std::size_t end = raw.find(';', index);
        if (end == std::string_view::npos) {
            result.push_back(c);
            ++index;
            continue;
        }
        const std::string_view name = raw.substr(index + 1, end - index - 1);
        if (name == "amp") result.push_back('&');
        else if (name == "lt") result.push_back('<');
        else if (name == "gt") result.push_back('>');
        else if (name == "quot") result.push_back('"');
        else if (name == "apos") result.push_back('\'');
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits(name.substr(hex ? 2 : 1));
            util::append_utf8(result, static_cast<char32_t>(std::strtoul(
                                          digits.c_str(), nullptr,
                                          hex ? 16 : 10)));
        } else {
            result.append(raw.substr(index, end - index + 1));
        }
        index = end + 1;
    }
    return result;
}

// Holds scanner state for one parse.
class scanner {
public:
    scanner(std::string_view text, xml_handler &handler, std::string &error)
        : text_(text), handler_(handler), error_(error) {
    }

    bool run() {
        while (at_ < text_.size()) {
            if (text_[at_] != '<') {
                const std::size_t next = text_.find('<', at_);
                const std::size_t end =
                    next == std::string_view::npos ? text_.size() : next;
                if (!open_.empty())
                    handler_.characters(
                        decoded(text_.substr(at_, end - at_)));
                at_ = end;
                continue;
            }
            if (!markup())
                return false;
        }
        if (!open_.empty())
            return fail("unclosed element " + open_.back());
        return true;
    }

private:
    bool fail(const std::string &message) {
        error_ = message + " at byte " + std::to_string(at_);
        return false;
    }

    // Skip past a terminator, failing when it is missing.
    bool skip_to(std::string_view terminator) {
        const std::size_t end = text_.find(terminator, at_);
        if (end == std::string_view::npos)
            return fail("unterminated markup");
        at_ = end + terminator.size();
        return true;
    }

    bool markup() {
        const std::string_view rest = text_.substr(at_);
        if (rest.starts_with("<!--"))
            return skip_to("-->");
        if (rest.starts_with("<?"))
            return skip_to("?>");
        if (rest.starts_with("<![CDATA[")) {
            at_ += 9;
            const std::size_t end = text_.find("]]>", at_);
            if (end == std::string_view::npos)
                return fail("unterminated CDATA");
            handler_.characters(text_.substr(at_, end - at_));
            at_ = end + 3;
            return true;
        }
        if (rest.starts_with("<!"))
            return skip_declaration();
        if (rest.starts_with("</"))
            return close_tag();
        return open_tag();
    }

    bool skip_declaration() {
        int depth = 0;
        for (; at_ < text_.size(); ++at_) {
            if (text_[at_] == '[' || text_[at_] == '<') ++depth;
            else if (text_[at_] == ']') --depth;
            else if (text_[at_] == '>' && --depth <= 0) {
                ++at_;
                return true;
            }
        }
        return fail("unterminated declaration");
    }

    std::string_view read_name() {
        const std::size_t start = at_;
        while (at_ < text_.size() && is_name_character(text_[at_]))
            ++at_;
        return text_.substr(start, at_ - start);
    }

    void skip_blanks() {
        while (at_ < text_.size() && is_blank(text_[at_]))
            ++at_;
    }

    bool close_tag() {
        at_ += 2;
        const std::string_view name = read_name();
        skip_blanks();
        if (at_ >= text_.size() || text_[at_] != '>')
            return fail("malformed closing tag");
        ++at_;
        if (open_.empty() || open_.back() != name)
            return fail("mismatched closing tag");
        open_.pop_back();
        handler_.end_element(name);
        return true;
    }

    bool open_tag() {
        ++at_;
        const std::string_view name = read_name();
        if (name.empty())
            return fail("malformed tag");
        xml_attributes attributes;
        for (;;) {
            skip_blanks();
            if (at_ >= text_.size())
                return fail("unterminated tag");
            if (text_[at_] == '>') {
                ++at_;
                open_.emplace_back(name);
                handler_.start_element(name, attributes);
                return true;
            }
            if (text_.substr(at_, 2) == "/>") {
                at_ += 2;
                handler_.start_element(name, attributes);
                handler_.end_element(name);
                return true;
            }
            if (!attribute(attributes))
                return false;
        }
    }

    bool attribute(xml_attributes &attributes) {
        const std::string_view name = read_name();
        skip_blanks();
        if (name.empty() || at_ >= text_.size() || text_[at_] != '=')
            return fail("malformed attribute");
        ++at_;
        skip_blanks();
        if (at_ >= text_.size() ||
            (text_[at_] != '"' && text_[at_] != '\''))
            return fail("unquoted attribute");
        const char quote = text_[at_++];
        const std::size_t end = text_.find(quote, at_);
        if (end == std::string_view::npos)
            return fail("unterminated attribute");
        attributes.insert_or_assign(std::string(name),
                                    decoded(text_.substr(at_, end - at_)));
        at_ = end + 1;
        return true;
    }

    std::string_view text_;
    xml_handler &handler_;
    std::string &error_;
    std::size_t at_ = 0;
    std::vector<std::string> open_;
};

} // namespace

bool parse_xml(std::string_view text, xml_handler &handler,
               std::string &error) {
    return scanner(text, handler, error).run();
}

std::string xml_escape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result.push_back(c); break;
        }
    }
    return result;
}

} // namespace util
