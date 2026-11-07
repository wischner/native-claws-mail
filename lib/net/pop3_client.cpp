//
// POP3.
//
// Derived from Claws Mail src/pop.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "net/pop3_client.h"

#include <map>

#include "util/codec.h"
#include "util/text.h"

namespace net {

pop3_client::pop3_client(const endpoint &target, const connector &connect)
    : stream_(connect(target)) {
    const std::string greeting = stream_.read_line();
    if (!greeting.starts_with("+OK"))
        throw net_error("POP3 server refused the connection: " + greeting);
    const std::size_t open = greeting.find('<');
    const std::size_t close = greeting.find('>', open);
    if (open != std::string::npos && close != std::string::npos)
        apop_stamp_ = greeting.substr(open, close - open + 1);
    if (target.tls == tls_mode::starttls) {
        command("STLS");
        stream_.start_tls();
        apop_stamp_.clear(); // The pre-TLS greeting is not trustworthy.
    }
}

std::string pop3_client::command(std::string_view line, bool secret) {
    stream_.write_line(line, secret);
    std::string reply = stream_.read_line();
    if (!reply.starts_with("+OK"))
        throw net_error("POP3 error: " + reply);
    return reply;
}

std::vector<std::string> pop3_client::read_multiline() {
    std::vector<std::string> lines;
    while (true) {
        std::string line = stream_.read_line();
        if (line == ".")
            return lines;
        if (line.starts_with('.'))
            line.erase(0, 1);
        lines.push_back(std::move(line));
    }
}

void pop3_client::login(const std::string &user, const std::string &password,
                        bool use_apop) {
    if (user.find_first_of("\r\n") != std::string::npos ||
        password.find_first_of("\r\n") != std::string::npos)
        throw auth_error("The login contains an illegal character.");
    try {
        if (use_apop) {
            if (apop_stamp_.empty())
                throw auth_error("The POP3 server does not offer APOP.");
            command("APOP " + user + " " +
                        util::md5_hex(apop_stamp_ + password),
                    true);
        } else {
            command("USER " + user);
            command("PASS " + password, true);
        }
    } catch (const auth_error &) {
        throw;
    } catch (const net_error &failure) {
        throw auth_error(failure.what());
    }
}

std::vector<pop3_entry> pop3_client::list() {
    std::vector<pop3_entry> entries;
    command("LIST");
    for (const std::string &line : read_multiline()) {
        const auto fields = util::split(line, ' ');
        if (fields.size() < 2)
            continue;
        const auto number = util::parse_int(fields[0]);
        const auto size = util::parse_int(fields[1]);
        if (number && size)
            entries.push_back({static_cast<int>(*number),
                               static_cast<std::uint64_t>(*size), {}});
    }
    try {
        command("UIDL");
    } catch (const net_error &) {
        return entries; // UIDL is optional; the caller copes without it.
    }
    std::map<int, std::string> uids;
    for (const std::string &line : read_multiline()) {
        const auto fields = util::split(line, ' ');
        if (fields.size() >= 2)
            if (const auto number = util::parse_int(fields[0]))
                uids[static_cast<int>(*number)] = fields[1];
    }
    for (pop3_entry &entry : entries)
        if (const auto it = uids.find(entry.number); it != uids.end())
            entry.uid = it->second;
    return entries;
}

std::string pop3_client::retrieve(int number) {
    command("RETR " + std::to_string(number));
    std::string message;
    for (const std::string &line : read_multiline()) {
        message += line;
        message += "\r\n";
    }
    return message;
}

void pop3_client::remove(int number) {
    command("DELE " + std::to_string(number));
}

void pop3_client::quit() { command("QUIT"); }

} // namespace net
