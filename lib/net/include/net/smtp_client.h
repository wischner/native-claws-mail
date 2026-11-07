//
// SMTP submission (RFC 5321) as a linear function instead of Claws'
// state machine in common/smtp.c.
//
// Derived from Claws Mail src/common/smtp.c and src/send_message.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef NET_SMTP_CLIENT_H
#define NET_SMTP_CLIENT_H

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "net/transport.h"

namespace net {

// Login details; an empty user means do not authenticate.
struct smtp_login {
    std::string user;
    std::string password;
};

// One SMTP session.
class smtp_client {
public:
    //
    // Connect and greet the server.
    //
    // Parameters:
    //      target      - The server.
    //      connect     - Opens the transport.
    //      hostname    - Name given to EHLO.
    //
    // Notes:
    //      With tls_mode::starttls a server that offers no STARTTLS is an
    //      error, so a man in the middle cannot strip the upgrade.
    //
    smtp_client(const endpoint &target, const connector &connect,
                std::string hostname);

    // Authenticate with the strongest offered of CRAM-MD5, PLAIN, LOGIN.
    void authenticate(const smtp_login &login);

    //
    // Send one message.
    //
    // Parameters:
    //      sender      - Envelope sender address.
    //      recipients  - Envelope recipient addresses.
    //      message     - Message text with CRLF line ends.
    //
    // Notes:
    //      Throws if any recipient is refused, before any data is sent.
    //
    void send(std::string_view sender,
              const std::vector<std::string> &recipients,
              std::string_view message);

    // Say goodbye; failures are ignored, the mail has already gone.
    void quit();

private:
    struct reply {
        int code = 0;
        std::string text;
    };

    reply read_reply();
    reply command(std::string_view line, bool secret = false);
    reply expect(std::string_view line, int code, bool secret = false);
    void hello();

    line_stream stream_;
    std::string hostname_;
    std::set<std::string> auth_methods_;
    bool can_starttls_ = false;
};

// Return message text with leading dots doubled, as DATA requires.
std::string dot_stuff(std::string_view message);

} // namespace net

#endif
