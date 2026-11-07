//
// Laying a message out for reading.
//
// Derived from Claws Mail src/textview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "view/document.h"

#include <algorithm>
#include <array>

#include "compose/draft.h"
#include "mime/encoded_word.h"
#include "mime/part.h"
#include "util/text.h"
#include "view/summary.h"

namespace view {
namespace {

constexpr std::array<const char *, 6> shown_headers{
    "Date", "From", "To", "Cc", "Newsgroups", "Subject"};

// Report whether a character can continue a URL.
bool is_url_character(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte > 32 && byte < 127 && c != '<' && c != '>' && c != '"';
}

// Return where a URL starting at an offset ends, trailing punctuation
// left out.
std::size_t url_end(std::string_view text, std::size_t start) {
    std::size_t end = start;
    while (end < text.size() && is_url_character(text[end]))
        ++end;
    while (end > start &&
           std::string_view(".,;:!?)'").find(text[end - 1]) !=
               std::string_view::npos)
        --end;
    return end;
}

// Appends text and records its spans.
class builder {
public:
    document result;

    // Append text in one style.
    void add(std::string_view text, text_style style) {
        if (text.empty())
            return;
        const std::size_t start = result.text.size();
        result.text += text;
        if (style != text_style::plain)
            result.spans.push_back({start, result.text.size(), style});
    }

    // Append body text, finding quotes, the signature and links.
    void add_body(std::string_view body) {
        bool signature = false;
        for (const std::string &line : util::split_lines(body)) {
            signature |= line == "-- ";
            const int depth = quote_depth(line);
            text_style style = text_style::plain;
            if (signature)
                style = text_style::signature;
            else if (depth > 0)
                style = depth == 1   ? text_style::quote_1
                        : depth == 2 ? text_style::quote_2
                                     : text_style::quote_3;
            add_line(line, style);
            result.text += '\n';
        }
    }

private:
    // Append one line, styling its links.
    void add_line(std::string_view line, text_style style) {
        std::size_t position = 0;
        while (position < line.size()) {
            const std::size_t start = next_link(line, position);
            if (start == std::string_view::npos)
                break;
            const std::size_t end = url_end(line, start);
            if (end <= start + 3) {
                add(line.substr(position, end - position), style);
                position = end;
                continue;
            }
            add(line.substr(position, start - position), style);
            const std::size_t at = result.text.size();
            add(line.substr(start, end - start), text_style::link);
            std::string target(line.substr(start, end - start));
            if (target.find("://") == std::string::npos &&
                !util::istarts_with(target, "mailto:"))
                target = "http://" + target;
            result.links.push_back({at, result.text.size(), target});
            position = end;
        }
        add(line.substr(position), style);
    }

    // Return the next offset where a URL starts.
    static std::size_t next_link(std::string_view line, std::size_t from) {
        std::size_t best = std::string_view::npos;
        for (const std::string_view scheme :
             {"http://", "https://", "ftp://", "mailto:", "www."}) {
            const std::size_t hit = util::ifind(line, scheme, from);
            if (hit != std::string_view::npos)
                best = std::min(best, hit);
        }
        return best;
    }
};

} // namespace

const link_span *document::link_at(std::size_t offset) const {
    for (const link_span &link : links)
        if (offset >= link.start && offset < link.end)
            return &link;
    return nullptr;
}

std::optional<std::size_t> document::attachment_at(std::size_t offset) const {
    for (std::size_t i = 0; i < attachments.size(); ++i)
        if (offset >= attachments[i].line_start &&
            offset <= attachments[i].line_end)
            return i;
    return std::nullopt;
}

int quote_depth(std::string_view line) {
    int depth = 0;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '>' || line[i] == '|')
            ++depth;
        else if (line[i] != ' ' || depth == 0)
            break;
        ++i;
    }
    return depth;
}

document render_document(std::string_view raw, bool full_headers,
                         int wrap_width) {
    const mime::part root = mime::parse_message(raw);
    builder out;
    const auto add_header = [&](std::string_view name, std::string_view value) {
        out.add(name, text_style::header_name);
        out.add(": ", text_style::header_name);
        const bool subject = util::iequals(name, "subject");
        out.add(mime::decode_words(value),
                subject ? text_style::subject : text_style::header_value);
        out.add("\n", text_style::plain);
    };
    if (full_headers) {
        for (const mime::header_field &field : root.headers.fields())
            add_header(field.name, field.value);
    } else {
        for (const char *name : shown_headers)
            if (const auto value = root.headers.find(name))
                add_header(name, *value);
    }
    out.add("\n", text_style::plain);
    const std::string body = mime::body_text(root, raw);
    out.add_body(wrap_width > 0 ? compose::wrap_text(body, wrap_width)
                                : body);
    const auto files = mime::attachments(root);
    if (!files.empty())
        out.add("\n", text_style::plain);
    for (const mime::part *file : files) {
        const std::size_t start = out.result.text.size();
        const std::string name =
            file->filename.empty() ? "(unnamed)" : file->filename;
        std::string line = "[Attachment: " + name + " (" +
                           file->mime_type() + ", " +
                           size_text(file->approximate_size()) + ")]";
        out.add(line, text_style::attachment);
        out.result.attachments.push_back({name, file->mime_type(),
                                          file->approximate_size(), start,
                                          out.result.text.size()});
        out.add("\n", text_style::plain);
    }
    return std::move(out.result);
}

std::string attachment_data(std::string_view raw, std::size_t index) {
    const mime::part root = mime::parse_message(raw);
    const auto files = mime::attachments(root);
    return index < files.size() ? files[index]->decoded_body(raw)
                                : std::string();
}

} // namespace view
