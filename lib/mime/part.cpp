//
// MIME parsing.
//
// Derived from Claws Mail src/procmime.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/part.h"

#include "mime/encoded_word.h"
#include "mime/html_text.h"
#include "util/charset.h"
#include "util/codec.h"
#include "util/text.h"

namespace mime {
namespace {

constexpr int max_depth = 32;

// Read the MIME fields of a block into a part.
void read_fields(part &node) {
    const header_block &fields = node.headers;
    if (const auto type = fields.find("Content-Type")) {
        const parameterized_value parsed = parse_parameters(*type);
        const std::size_t slash = parsed.value.find('/');
        if (slash != std::string::npos && slash != 0) {
            node.type = parsed.value.substr(0, slash);
            node.subtype = parsed.value.substr(slash + 1);
        }
        const auto parameter = [&](const char *name) {
            const auto it = parsed.parameters.find(name);
            return it == parsed.parameters.end() ? std::string()
                                                 : it->second;
        };
        node.charset = parameter("charset");
        node.boundary = parameter("boundary");
        node.filename = parameter("name");
    }
    if (const auto disposition = fields.find("Content-Disposition")) {
        const parameterized_value parsed = parse_parameters(*disposition);
        node.attachment = parsed.value == "attachment";
        if (const auto it = parsed.parameters.find("filename");
            it != parsed.parameters.end())
            node.filename = it->second;
    }
    const std::string encoding =
        util::lower(fields.get("Content-Transfer-Encoding"));
    if (encoding.find("quoted-printable") != std::string::npos)
        node.encoding = transfer_encoding::quoted_printable;
    else if (encoding.find("base64") != std::string::npos)
        node.encoding = transfer_encoding::base64;
    else if (encoding.find("uuencode") != std::string::npos ||
             encoding.find("x-uue") != std::string::npos)
        node.encoding = transfer_encoding::uuencode;
    node.description = decode_words(fields.get("Content-Description"));
    node.content_id = std::string(
        util::trim(fields.get("Content-ID")));
    if (node.content_id.size() >= 2 && node.content_id.front() == '<')
        node.content_id =
            node.content_id.substr(1, node.content_id.size() - 2);
}

// A delimiter line located in a multipart body.
struct delimiter {
    std::size_t line_start;
    std::size_t next_line;
    bool closing;
};

// Find the next delimiter of a boundary inside [from, end).
bool find_delimiter(std::string_view raw, std::string_view boundary,
                    std::size_t from, std::size_t end, delimiter &found) {
    std::size_t position = from;
    while (position < end) {
        std::size_t stop = raw.find('\n', position);
        if (stop == std::string_view::npos || stop >= end)
            stop = end;
        std::string_view line = raw.substr(position, stop - position);
        if (line.starts_with("--") &&
            line.substr(2).starts_with(boundary)) {
            std::string_view rest = line.substr(2 + boundary.size());
            const bool closing = rest.starts_with("--");
            if (closing || util::trim(rest).empty()) {
                found = {position, stop < end ? stop + 1 : end, closing};
                return true;
            }
        }
        position = stop + 1;
    }
    return false;
}

part parse_entity(std::string_view raw, std::size_t start, std::size_t end,
                  int depth);

// Parse the children of a multipart body.
void parse_multipart(std::string_view raw, part &node, int depth) {
    delimiter current{};
    if (node.boundary.empty() ||
        !find_delimiter(raw, node.boundary, node.body_start, node.body_end,
                        current))
        return;
    while (!current.closing) {
        delimiter next{};
        const bool more = find_delimiter(raw, node.boundary,
                                         current.next_line, node.body_end,
                                         next);
        std::size_t stop = more ? next.line_start : node.body_end;
        // The line end before a delimiter belongs to the delimiter.
        if (more && stop > current.next_line && raw[stop - 1] == '\n')
            --stop;
        if (more && stop > current.next_line && raw[stop - 1] == '\r')
            --stop;
        node.children.push_back(
            parse_entity(raw, current.next_line, stop, depth + 1));
        if (!more)
            break;
        current = next;
    }
}

// Parse one entity occupying [start, end).
part parse_entity(std::string_view raw, std::size_t start, std::size_t end,
                  int depth) {
    part node;
    std::size_t offset = 0;
    node.headers =
        header_block::parse(raw.substr(start, end - start), &offset);
    node.body_start = start + offset;
    node.body_end = end;
    read_fields(node);
    if (depth >= max_depth)
        return node;
    if (node.is_multipart()) {
        parse_multipart(raw, node, depth);
    } else if (node.type == "message" && node.subtype == "rfc822" &&
               node.encoding == transfer_encoding::identity) {
        node.children.push_back(
            parse_entity(raw, node.body_start, node.body_end, depth + 1));
    }
    return node;
}

// Decode a uuencoded body.
std::string uudecode(std::string_view text) {
    std::string out;
    for (const std::string &line : util::split_lines(text)) {
        if (line.empty() || line.starts_with("begin ") || line == "end" ||
            line == "`")
            continue;
        const int count = (line[0] - 32) & 63;
        if (count == 0)
            continue;
        std::uint32_t chunk = 0;
        int bits = 0;
        int written = 0;
        for (std::size_t i = 1; i < line.size() && written < count; ++i) {
            chunk = chunk << 6 | (static_cast<unsigned>(line[i] - 32) & 63);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>(chunk >> bits & 255);
                ++written;
            }
        }
    }
    return out;
}

// Return the first leaf of a kind, searching depth first.
const part *find_leaf(const part &root, std::string_view subtype) {
    const part *found = nullptr;
    for_each_leaf(root, [&](const part &leaf) {
        if (found == nullptr && leaf.type == "text" &&
            leaf.subtype == subtype && !leaf.attachment)
            found = &leaf;
    });
    return found;
}

// Return the best alternative of a multipart/alternative.
const part *best_alternative(const part &root) {
    const part *fallback = nullptr;
    for (const part &child : root.children) {
        if (child.type == "text" && child.subtype == "plain")
            return &child;
        if (const part *inner = body_part(child);
            inner != nullptr && fallback == nullptr)
            fallback = inner;
    }
    return fallback;
}

} // namespace

std::string part::mime_type() const { return type + "/" + subtype; }

std::string_view part::raw_body(std::string_view message) const {
    return message.substr(body_start, body_end - body_start);
}

std::string part::decoded_body(std::string_view message) const {
    const std::string_view raw = raw_body(message);
    switch (encoding) {
    case transfer_encoding::quoted_printable:
        return util::qp_decode(raw);
    case transfer_encoding::base64:
        return util::base64_decode(raw);
    case transfer_encoding::uuencode:
        return uudecode(raw);
    case transfer_encoding::identity:
        break;
    }
    return std::string(raw);
}

std::string part::text(std::string_view message) const {
    return util::normalize_newlines(
        util::to_utf8(decoded_body(message), charset));
}

std::size_t part::approximate_size() const {
    const std::size_t stored = body_end - body_start;
    return encoding == transfer_encoding::base64 ? stored / 4 * 3 : stored;
}

part parse_message(std::string_view raw) {
    return parse_entity(raw, 0, raw.size(), 0);
}

const part *body_part(const part &root) {
    if (root.subtype == "alternative" && root.is_multipart())
        return best_alternative(root);
    if (root.type == "message" && !root.children.empty())
        return body_part(root.children.front());
    if (root.is_multipart()) {
        for (const part &child : root.children) {
            if (child.attachment)
                continue;
            if (const part *inner = body_part(child))
                return inner;
        }
        return nullptr;
    }
    return root.type == "text" && !root.attachment ? &root : nullptr;
}

std::vector<const part *> attachments(const part &root) {
    std::vector<const part *> found;
    const part *body = body_part(root);
    const auto visit = [&](const auto &self, const part &node,
                           bool in_alternative) -> void {
        if (!node.children.empty()) {
            const bool alternative =
                in_alternative ||
                (node.is_multipart() && node.subtype == "alternative");
            for (const part &child : node.children)
                self(self, child, alternative);
            return;
        }
        if (&node == body || (in_alternative && !node.attachment))
            return;
        if (node.is_multipart())
            return;
        found.push_back(&node);
    };
    visit(visit, root, false);
    return found;
}

std::string body_text(const part &root, std::string_view message) {
    const part *body = body_part(root);
    if (body == nullptr) {
        if (const part *html = find_leaf(root, "html"))
            body = html;
        else
            return {};
    }
    if (body->subtype == "html")
        return html_to_text(body->text(message));
    if (body->subtype == "enriched" || body->subtype == "richtext")
        return enriched_to_text(body->text(message));
    return body->text(message);
}

} // namespace mime
