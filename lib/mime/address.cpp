//
// RFC 5322 address parsing.
//
// Derived from Claws Mail src/common/utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/address.h"

#include "util/text.h"

namespace mime {
namespace {

// Walks an address list one mailbox at a time.
class address_parser {
public:
    explicit address_parser(std::string_view text) : text_(text) {}

    // Parse everything.
    std::vector<mailbox> run() {
        std::vector<mailbox> boxes;
        while (position_ < text_.size())
            parse_one(boxes);
        return boxes;
    }

private:
    // Parse up to the next top-level comma.
    void parse_one(std::vector<mailbox> &boxes) {
        std::string phrase;
        std::string comment;
        std::string angle;
        bool quoted_name = false;
        bool have_angle = false;
        bool in_angle = false;
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == '"') {
                phrase += read_quoted();
                quoted_name = true;
                continue;
            }
            if (c == '(') {
                comment = read_comment();
                continue;
            }
            ++position_;
            if (c == '<' && !in_angle) {
                in_angle = have_angle = true;
            } else if (c == '>' && in_angle) {
                in_angle = false;
            } else if (in_angle) {
                angle += c;
            } else if (c == ':') {
                phrase.clear(); // A group name; its members follow.
                quoted_name = false;
            } else if (c == ',' || c == ';') {
                break;
            } else {
                phrase += c;
            }
        }
        std::string spec = have_angle ? std::string(util::trim(angle))
                                      : std::string();
        std::string name(util::trim(phrase));
        if (!have_angle) {
            if (quoted_name && name.find('@') == std::string::npos) {
                if (!comment.empty())
                    return;
            }
            spec = name;
            name = comment;
        } else if (name.empty()) {
            name = comment;
        }
        if (!spec.empty())
            boxes.push_back({std::move(name), std::move(spec)});
    }

    // Read a quoted string starting at its opening quote.
    std::string read_quoted() {
        std::string out;
        ++position_;
        while (position_ < text_.size() && text_[position_] != '"') {
            if (text_[position_] == '\\' && position_ + 1 < text_.size())
                ++position_;
            out += text_[position_++];
        }
        ++position_;
        return out;
    }

    // Read a comment starting at its opening parenthesis.
    std::string read_comment() {
        std::string out;
        int depth = 0;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '\\' && position_ < text_.size()) {
                out += text_[position_++];
                continue;
            }
            if (c == '(' && depth++ == 0)
                continue;
            if (c == ')' && --depth == 0)
                break;
            out += c;
        }
        return out;
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

// Report whether a name can stand unquoted as an RFC 5322 phrase.
bool is_plain_phrase(std::string_view name) {
    for (const char c : name) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ' ' ||
                           std::string_view("!#$%&'*+-/=?^_`{|}~")
                                   .find(c) != std::string_view::npos ||
                           static_cast<unsigned char>(c) >= 128;
        if (!plain)
            return false;
    }
    return true;
}

} // namespace

std::vector<mailbox> parse_addresses(std::string_view text) {
    return address_parser(text).run();
}

mailbox parse_address(std::string_view text) {
    const auto boxes = parse_addresses(text);
    return boxes.empty() ? mailbox{} : boxes.front();
}

std::string format_mailbox(const mailbox &box) {
    if (box.name.empty())
        return box.address;
    if (is_plain_phrase(box.name))
        return box.name + " <" + box.address + ">";
    std::string quoted = "\"";
    for (const char c : box.name) {
        if (c == '"' || c == '\\')
            quoted += '\\';
        quoted += c;
    }
    return quoted + "\" <" + box.address + ">";
}

std::string format_addresses(const std::vector<mailbox> &boxes) {
    std::vector<std::string> parts;
    parts.reserve(boxes.size());
    for (const mailbox &box : boxes)
        parts.push_back(format_mailbox(box));
    return util::join(parts, ", ");
}

std::string display_name(std::string_view text) {
    const mailbox box = parse_address(text);
    if (!box.name.empty())
        return box.name;
    return box.address.empty() ? std::string(util::trim(text)) : box.address;
}

} // namespace mime
