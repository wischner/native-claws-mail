//
// IMAP4rev1 (RFC 3501). Claws drives libetpan from a thread and marshals
// each call back through imap-thread.c; here the client itself is
// blocking and owns the wire, so src/imap.c's five thousand lines shrink
// to one parser, one command wrapper per verb, and mail::imap_folder.
//
// Derived from Claws Mail src/imap.c and src/etpan/imap-thread.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef NET_IMAP_CLIENT_H
#define NET_IMAP_CLIENT_H

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "net/transport.h"

namespace net {

// One mailbox from LIST.
struct imap_mailbox {
    std::string name;   // Decoded from modified UTF-7 to UTF-8.
    std::string raw_name;
    char delimiter = '/';
    bool no_select = false;
    bool no_inferiors = false;
};

// One message's fetched attributes.
struct imap_message {
    std::uint32_t uid = 0;
    std::uint64_t size = 0;
    std::set<std::string> flags;    // As the server spells them.
    std::string headers;            // Present when asked for.
    std::string body;               // Present when asked for.
};

// One token of a response: an atom, a string or a parenthesized list.
struct imap_item {
    enum class kind { atom, string, list, nil };

    kind type = kind::atom;
    std::string text;
    std::vector<imap_item> children;
};

// What SELECT reported.
struct imap_selection {
    std::uint32_t exists = 0;
    std::uint32_t uid_validity = 0;
    std::uint32_t uid_next = 0;
    bool read_only = false;
};

// One IMAP session.
class imap_client {
public:
    // Connect and read the greeting; upgrades to TLS as the target asks.
    imap_client(const endpoint &target, const connector &connect);

    // Report whether the server was pre-authenticated or logged in.
    bool authenticated() const { return authenticated_; }

    // Log in with LOGIN or AUTHENTICATE CRAM-MD5/PLAIN, whichever offered.
    void login(const std::string &user, const std::string &password);

    // Return mailboxes, or the subscribed ones.
    std::vector<imap_mailbox> list(std::string_view reference,
                                   std::string_view pattern,
                                   bool subscribed_only);

    // Select a mailbox.
    imap_selection select(std::string_view raw_name);

    // Return every UID in the selected mailbox.
    std::vector<std::uint32_t> search_all();

    //
    // Fetch attributes for a UID set.
    //
    // Parameters:
    //      uids        - An IMAP sequence set such as "1:*" or "4,7".
    //      headers     - Also fetch the header block, without setting
    //                    \Seen.
    //
    std::vector<imap_message> fetch_summaries(std::string_view uids,
                                              bool headers);

    // Fetch one complete message without setting \Seen.
    std::optional<std::string> fetch_message(std::uint32_t uid);

    // Add or remove flags on a UID.
    void store_flags(std::uint32_t uid, bool add, std::string_view flags);

    // Copy a UID to another mailbox; returns its new UID, when reported.
    std::uint32_t copy(std::uint32_t uid, std::string_view raw_name);

    // Append a message; returns its new UID, when the server reports it.
    std::uint32_t append(std::string_view raw_name, std::string_view message,
                         std::string_view flags);

    // Permanently remove messages flagged deleted, within a UID.
    void expunge(std::uint32_t uid);

    // Create a mailbox.
    void create(std::string_view raw_name);

    // Delete a mailbox.
    void remove(std::string_view raw_name);

    // Rename a mailbox.
    void rename(std::string_view from, std::string_view to);

    // Keep the session alive and learn of new mail.
    void noop();

    // End the session.
    void logout();

private:
    // One parsed response.
    struct response {
        std::string tag;
        std::string status;         // OK, NO, BAD, BYE or PREAUTH.
        std::string text;
        std::string code;           // The [BRACKETED] part of a status.
        std::vector<imap_item> items;
    };

    using untagged_handler = std::function<void(const response &)>;

    std::string next_tag();
    response read_response();
    void parse_items(std::string line, std::vector<imap_item> &into);
    response run(const std::string &command, bool secret = false,
                 const untagged_handler &untagged = {});
    response finish(const std::string &tag,
                    const untagged_handler &untagged);
    void require_ok(const response &result, std::string_view what) const;
    void note_capabilities(const response &item);
    void read_capabilities();
    void parse_selection(const response &item, imap_selection &selection);
    std::string literal_or_quoted(std::string_view text);

    line_stream stream_;
    int tag_serial_ = 0;
    std::set<std::string> capabilities_;
    bool authenticated_ = false;
};

// Return text as an IMAP quoted string.
std::string imap_quote(std::string_view text);

// Decode IMAP's modified UTF-7 to UTF-8.
std::string imap_utf7_decode(std::string_view name);

// Encode UTF-8 as IMAP's modified UTF-7.
std::string imap_utf7_encode(std::string_view name);

} // namespace net

#endif
