//
// The queue format. A queued message is the finished message behind a
// block of routing lines, in Claws' layout, so its queue folder and
// Blaise's interchange.
//
// Derived from Claws Mail src/compose.c (compose_queue_sub) and
// src/procmsg.c (procmsg_send_message_queue),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef COMPOSE_QUEUE_H
#define COMPOSE_QUEUE_H

#include <string>
#include <string_view>
#include <vector>

namespace compose {

// A parsed queue file.
struct queued_message {
    std::string sender;
    std::string smtp_server;
    std::vector<std::string> recipients;
    int account_id = 0;
    std::string sent_folder;    // Identifier, or empty for none.
    std::string reply_source;   // "folder-id\tnumber", marked replied.
    std::string forward_source; // Marked forwarded.
    std::string message;        // What goes over the wire.
};

//
// Wrap a rendered message for the queue.
//
// Parameters:
//      item        - All fields of the entry, with message set.
//
std::string write_queue_entry(const queued_message &item);

// Parse a queue file; a message with no routing block keeps its text.
queued_message read_queue_entry(std::string_view text);

} // namespace compose

#endif
