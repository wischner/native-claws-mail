//
// Message rendering.
//
// Derived from Claws Mail src/compose.c and src/procmime.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mime/builder.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#include "mime/address.h"
#include "mime/encoded_word.h"
#include "util/charset.h"
#include "util/codec.h"
#include "util/date.h"
#include "util/text.h"

namespace mime {
namespace {

// A file extension and its type.
struct mime_guess {
    const char *extension;
    const char *type;
};

constexpr std::array<mime_guess, 26> guesses{{
    {".txt", "text/plain"},
    {".html", "text/html"},
    {".htm", "text/html"},
    {".csv", "text/csv"},
    {".md", "text/markdown"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".webp", "image/webp"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".tar", "application/x-tar"},
    {".xml", "application/xml"},
    {".json", "application/json"},
    {".doc", "application/msword"},
    {".abw", "application/x-abiword"},
    {".gnumeric", "application/x-gnumeric"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".mp3", "audio/mpeg"},
    {".ogg", "audio/ogg"},
    {".mp4", "video/mp4"},
    {".eml", "message/rfc822"},
}};

// Return the longest line in text.
std::size_t longest_line(std::string_view text) {
    std::size_t longest = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        longest = std::max(longest, end - start);
        start = end + 1;
    }
    return longest;
}

// Report whether a line of the text starts "From " or a lone dot, which
// relays mangle; quoted printable protects them.
bool needs_protection(std::string_view text) {
    return text.starts_with("From ") ||
           text.find("\nFrom ") != std::string_view::npos ||
           longest_line(text) > 998;
}

// Return the transfer encoding and content of the text body.
void write_text_part(std::string &out, std::string_view body) {
    const bool ascii = util::is_ascii(body);
    const bool quoted = !ascii || needs_protection(body);
    out += "Content-Type: text/plain; charset=";
    out += ascii ? "us-ascii" : "UTF-8";
    out += "\n";
    out += "Content-Transfer-Encoding: ";
    out += quoted ? "quoted-printable" : "7bit";
    out += "\n\n";
    out += quoted ? util::qp_encode(body) : std::string(body);
}

// Return a filename parameter, RFC 2231 encoded when it is not ASCII.
std::string filename_parameter(const std::string &name) {
    if (util::is_ascii(name)) {
        std::string quoted = "filename=\"";
        for (const char c : name) {
            if (c == '"' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        return quoted + "\"";
    }
    std::string encoded = "filename*=UTF-8''";
    static constexpr std::string_view digits = "0123456789ABCDEF";
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += digits[byte >> 4];
            encoded += digits[byte & 15];
        }
    }
    return encoded;
}

// Write one attachment part.
void write_attachment(std::string &out, const attachment_data &item) {
    const std::string type =
        item.mime_type.empty() ? "application/octet-stream" : item.mime_type;
    out += "Content-Type: " + type + "\n";
    out += "Content-Disposition: attachment; " +
           filename_parameter(item.filename) + "\n";
    out += "Content-Transfer-Encoding: base64\n\n";
    out += util::normalize_newlines(util::base64_encode(item.data, 76));
}

// Add a field unless the value is empty.
void add_field(header_block &headers, std::string_view name,
               std::string_view value) {
    if (!value.empty())
        headers.add(name, value);
}

} // namespace

std::string render_message(const outgoing_message &message,
                           const std::string &boundary) {
    header_block headers;
    add_field(headers, "Date",
              util::format_mail_date(message.date,
                                     util::local_utc_offset(message.date)));
    add_field(headers, "From", encode_address_list(message.from));
    add_field(headers, "To", encode_address_list(message.to));
    add_field(headers, "Cc", encode_address_list(message.cc));
    add_field(headers, "Reply-To", encode_address_list(message.reply_to));
    add_field(headers, "Subject", encode_words(message.subject));
    if (!message.message_id.empty())
        headers.add("Message-ID", "<" + message.message_id + ">");
    add_field(headers, "In-Reply-To", message.in_reply_to);
    add_field(headers, "References", message.references);
    add_field(headers, "Organization", encode_words(message.organization));
    add_field(headers, "User-Agent", message.user_agent);
    for (const header_field &field : message.extra_headers)
        headers.add(field.name, encode_words(field.value));
    headers.add("MIME-Version", "1.0");

    std::string out;
    for (const header_field &field : headers.fields()) {
        out += fold_header(field.name, field.value);
        out += '\n';
    }
    if (message.attachments.empty()) {
        write_text_part(out, message.body);
    } else {
        const std::string divider =
            boundary.empty()
                ? "=-blaise-" + util::md5_hex(message.body + message.message_id)
                : boundary;
        out += "Content-Type: multipart/mixed;\n boundary=\"" + divider +
               "\"\n\nThis is a multi-part message in MIME format.\n";
        out += "\n--" + divider + "\n";
        write_text_part(out, message.body);
        // Each delimiter owns the line end before it, so parts keep theirs.
        for (const attachment_data &item : message.attachments) {
            out += "\n--" + divider + "\n";
            write_attachment(out, item);
        }
        out += "\n--" + divider + "--\n";
    }
    if (!out.ends_with('\n'))
        out += '\n';
    return util::to_crlf(out);
}

std::string make_message_id(std::string_view address, std::time_t when) {
    static std::atomic<unsigned> serial{0};
    const mailbox box = parse_address(address);
    const std::size_t at = box.address.rfind('@');
    const std::string domain = at == std::string::npos
                                   ? "localhost"
                                   : box.address.substr(at + 1);
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%llx.%x.blaise",
                  static_cast<unsigned long long>(when),
                  serial.fetch_add(1));
    return buffer + ("." + util::md5_hex(std::string(address) +
                                         std::to_string(when))
                               .substr(0, 8)) +
           "@" + domain;
}

std::string guess_mime_type(const std::filesystem::path &path) {
    const std::string extension = util::lower(path.extension().string());
    for (const mime_guess &guess : guesses)
        if (extension == guess.extension)
            return guess.type;
    return "application/octet-stream";
}

} // namespace mime
