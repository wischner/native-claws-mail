//
// Writes messages. compose_write_to_file's stream of fprintf calls
// becomes a value that says what the message is, then renders it, so a
// test can inspect the bytes and every send path shares one writer.
//
// Derived from Claws Mail src/compose.c and src/procmime.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_BUILDER_H
#define MIME_BUILDER_H

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "mime/header.h"

namespace mime {

// One file or byte string to attach.
struct attachment_data {
    std::string filename;
    std::string mime_type;
    std::string data;
};

// A message waiting to be written.
struct outgoing_message {
    std::string from;
    std::string to;
    std::string cc;
    std::string bcc;
    std::string reply_to;
    std::string subject;
    std::string body;               // UTF-8, LF line ends.
    std::string organization;
    std::string in_reply_to;
    std::string references;
    std::string message_id;         // Without angle brackets.
    std::string user_agent;
    std::time_t date = 0;
    std::vector<header_field> extra_headers;
    std::vector<attachment_data> attachments;
};

//
// Render a message.
//
// Parameters:
//      message     - The message.
//      boundary    - The multipart boundary; empty derives one.
//
// Notes:
//      ASCII text stays 7bit, anything else is UTF-8 quoted printable,
//      and lines over 998 bytes force quoted printable so the result
//      passes any SMTP relay. Bcc is deliberately not written.
//
// Returns:
//      The message with CRLF line ends.
//
std::string render_message(const outgoing_message &message,
                           const std::string &boundary = {});

// Return a Message-ID, without brackets, for a sender's domain.
std::string make_message_id(std::string_view address, std::time_t when);

// Guess a MIME type from a file name.
std::string guess_mime_type(const std::filesystem::path &path);

} // namespace mime

#endif
