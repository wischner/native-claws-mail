//
// SMTP submission.
//
// Derived from Claws Mail src/common/smtp.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "net/smtp_client.h"

#include "util/codec.h"
#include "util/text.h"

namespace net {
namespace {

// Reject text that could end a command early.
void require_clean(std::string_view value) {
    if (value.find_first_of("\r\n<>") != std::string_view::npos)
        throw net_error("An address contains an illegal character.");
}

} // namespace

std::string dot_stuff(std::string_view message) {
    std::string out;
    out.reserve(message.size() + 64);
    bool line_start = true;
    for (const char c : message) {
        if (line_start && c == '.')
            out += '.';
        out += c;
        line_start = c == '\n';
    }
    return out;
}

smtp_client::smtp_client(const endpoint &target, const connector &connect,
                         std::string hostname)
    : stream_(connect(target)), hostname_(std::move(hostname)) {
    const reply greeting = read_reply();
    if (greeting.code != 220)
        throw net_error("SMTP server refused the connection: " +
                        greeting.text);
    hello();
    if (target.tls == tls_mode::starttls) {
        if (!can_starttls_)
            throw net_error("The SMTP server does not offer STARTTLS.");
        expect("STARTTLS", 220);
        stream_.start_tls();
        hello(); // Capabilities from before TLS cannot be trusted.
    }
}

smtp_client::reply smtp_client::read_reply() {
    reply result;
    while (true) {
        const std::string line = stream_.read_line();
        if (line.size() < 3)
            throw net_error("Malformed reply from the SMTP server.");
        const auto code = util::parse_int(std::string_view(line).substr(0, 3));
        if (!code)
            throw net_error("Malformed reply from the SMTP server.");
        result.code = static_cast<int>(*code);
        if (!result.text.empty())
            result.text += '\n';
        result.text += line.size() > 4 ? line.substr(4) : "";
        if (line.size() < 4 || line[3] != '-')
            return result;
    }
}

smtp_client::reply smtp_client::command(std::string_view line, bool secret) {
    stream_.write_line(line, secret);
    return read_reply();
}

smtp_client::reply smtp_client::expect(std::string_view line, int code,
                                       bool secret) {
    reply result = command(line, secret);
    if (result.code != code)
        throw net_error("SMTP error: " + result.text);
    return result;
}

void smtp_client::hello() {
    auth_methods_.clear();
    can_starttls_ = false;
    const reply result = command("EHLO " + hostname_);
    if (result.code != 250) {
        expect("HELO " + hostname_, 250);
        return;
    }
    for (const std::string &line : util::split_lines(result.text)) {
        const std::string upper = util::upper(line);
        if (upper == "STARTTLS") {
            can_starttls_ = true;
        } else if (upper.starts_with("AUTH ") ||
                   upper.starts_with("AUTH=")) {
            for (const std::string &method :
                 util::split(std::string_view(upper).substr(5), ' '))
                auth_methods_.insert(method);
        }
    }
}

void smtp_client::authenticate(const smtp_login &login) {
    if (login.user.empty())
        return;
    reply result;
    if (auth_methods_.contains("CRAM-MD5")) {
        result = command("AUTH CRAM-MD5");
        if (result.code == 334) {
            const std::string challenge = util::base64_decode(result.text);
            const std::string answer =
                login.user + " " +
                util::hmac_md5_hex(login.password, challenge);
            result = command(util::base64_encode(answer), true);
        }
    } else if (auth_methods_.contains("PLAIN")) {
        std::string token(1, '\0');
        token += login.user;
        token += '\0';
        token += login.password;
        result = command("AUTH PLAIN " + util::base64_encode(token), true);
    } else if (auth_methods_.contains("LOGIN")) {
        result = command("AUTH LOGIN");
        if (result.code == 334)
            result = command(util::base64_encode(login.user), true);
        if (result.code == 334)
            result = command(util::base64_encode(login.password), true);
    } else {
        throw auth_error("The SMTP server offers no login method Blaise "
                         "knows.");
    }
    if (result.code != 235)
        throw auth_error("SMTP login failed: " + result.text);
}

void smtp_client::send(std::string_view sender,
                       const std::vector<std::string> &recipients,
                       std::string_view message) {
    require_clean(sender);
    if (recipients.empty())
        throw net_error("The message has no recipients.");
    expect("MAIL FROM:<" + std::string(sender) + ">", 250);
    for (const std::string &recipient : recipients) {
        require_clean(recipient);
        const reply result = command("RCPT TO:<" + recipient + ">");
        if (result.code != 250 && result.code != 251)
            throw net_error("The server refused " + recipient + ": " +
                            result.text);
    }
    expect("DATA", 354);
    std::string body = dot_stuff(message);
    if (!body.ends_with("\r\n"))
        body += "\r\n";
    body += ".\r\n";
    stream_.write(body);
    const reply result = read_reply();
    if (result.code != 250)
        throw net_error("The server refused the message: " + result.text);
}

void smtp_client::quit() {
    try {
        command("QUIT");
    } catch (const net_error &) {
        // The message was accepted; a lost goodbye changes nothing.
    }
}

} // namespace net
