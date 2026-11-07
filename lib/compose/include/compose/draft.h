//
// A message being written. compose.c is twelve thousand lines of GTK
// signal handlers around one idea; that idea is this value, and the
// functions that make one from a message being replied to. The window
// shows a draft and edits it, so all of this runs without a display.
//
// Derived from Claws Mail src/compose.c and src/quote_fmt.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef COMPOSE_DRAFT_H
#define COMPOSE_DRAFT_H

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mail/msg_info.h"
#include "mime/builder.h"
#include "prefs/account.h"
#include "prefs/common.h"

namespace compose {

// How a reply chooses its recipients.
enum class reply_mode { sender, all, list };

// A file waiting to be attached.
struct attachment {
    std::filesystem::path path;
    std::string name;
    std::string mime_type;
    std::string data;   // Used instead of path when path is empty.
};

// A message being written.
struct draft {
    int account_id = 0;
    std::string from;
    std::string to;
    std::string cc;
    std::string bcc;
    std::string reply_to;
    std::string subject;
    std::string body;
    std::string in_reply_to;
    std::string references;
    std::vector<attachment> attachments;
    std::size_t cursor = 0;         // Byte offset for the caret.
    std::string reply_source;       // "folder-id\tnumber" of the original.
    std::string forward_source;
    std::string replaces;           // "folder-id\tnumber" this supersedes.

    // Return every recipient address.
    std::vector<std::string> recipients() const;
};

//
// Expand a quote format.
//
// Parameters:
//      format      - Claws' format language: %d %f %s %q %M and so on,
//                    with \\n and \\t escapes.
//      info        - The original message.
//      body        - Its text.
//      quote_mark  - Prefix for %q and %Q.
//      account     - The sending account, for %af and %am.
//      cursor      - Receives the offset of %X when present.
//
std::string expand_format(std::string_view format,
                          const mail::msg_info &info,
                          std::string_view body,
                          std::string_view quote_mark,
                          const prefs::account &account,
                          std::size_t *cursor = nullptr);

// Return body text with each line quoted.
std::string quote_text(std::string_view text, std::string_view mark);

// Return body text without its signature.
std::string strip_signature(std::string_view text);

//
// Wrap text to a width.
//
// Notes:
//      Quoted lines rewrap under their own prefix, and a word longer
//      than the width, such as a URL, stays whole on its own line.
//
std::string wrap_text(std::string_view text, int width);

// Start a new message from an account.
draft new_draft(const prefs::account &account);

//
// Start a reply.
//
// Parameters:
//      raw         - The original message.
//      account     - The account replying; its own address is left out.
//      settings    - Quote format and marks.
//      mode        - Who the reply goes to.
//
draft reply_to(std::string_view raw, const prefs::account &account,
               const prefs::common &settings, reply_mode mode);

// Start a forward, quoting the original in the body.
draft forward(std::string_view raw, const prefs::account &account,
              const prefs::common &settings);

// Reopen a saved draft or queued message for editing.
draft reopen(std::string_view raw);

// Append the account's signature to a body.
std::string with_signature(std::string_view body,
                           const prefs::account &account);

// Choose the account a reply should come from: the one addressed.
const prefs::account *account_for_reply(std::string_view raw,
                                        const prefs::account_list &accounts);

//
// Build the outgoing form of a draft.
//
// Parameters:
//      text        - The draft.
//      account     - The sending account.
//      settings    - Wrapping preferences.
//      now         - The send time.
//
// Returns:
//      The message, or nothing if an attachment cannot be read.
//
std::optional<mime::outgoing_message>
to_outgoing(const draft &text, const prefs::account &account,
            const prefs::common &settings, std::time_t now);

} // namespace compose

#endif
