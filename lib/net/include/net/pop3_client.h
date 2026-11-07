//
// POP3 (RFC 1939) as a plain class instead of the state machine in
// src/pop.c.
//
// Derived from Claws Mail src/pop.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef NET_POP3_CLIENT_H
#define NET_POP3_CLIENT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/transport.h"

namespace net {

// One message on the server.
struct pop3_entry {
    int number = 0;
    std::uint64_t size = 0;
    std::string uid;
};

// One POP3 session.
class pop3_client {
public:
    // Connect and read the greeting; upgrades to TLS as the target asks.
    pop3_client(const endpoint &target, const connector &connect);

    //
    // Log in.
    //
    // Parameters:
    //      user, password - Credentials.
    //      use_apop    - Use APOP; the server must offer it.
    //
    void login(const std::string &user, const std::string &password,
               bool use_apop);

    // List the mailbox with sizes and, where supported, unique ids.
    std::vector<pop3_entry> list();

    // Return one message's text, with CRLF line ends.
    std::string retrieve(int number);

    // Mark a message for deletion at quit.
    void remove(int number);

    // End the session, committing deletions.
    void quit();

private:
    std::string command(std::string_view line, bool secret = false);
    std::vector<std::string> read_multiline();

    line_stream stream_;
    std::string apop_stamp_;
};

} // namespace net

#endif
