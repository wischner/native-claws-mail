//
// In-memory servers. Each implements net::transport and answers the
// client as a real server would, so protocol tests need no sockets and
// run the same on every machine.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef TESTS_FAKE_SERVER_H
#define TESTS_FAKE_SERVER_H

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "net/transport.h"
#include "util/text.h"

namespace testing {

// Answers each complete line; subclasses hold the protocol.
class fake_transport : public net::transport {
public:
    std::size_t read(char *buffer, std::size_t size) override {
        if (outgoing_.empty()) {
            if (closed_)
                return 0;
            throw net::net_error("fake server has nothing to say");
        }
        const std::size_t count = std::min(size, outgoing_.size());
        std::memcpy(buffer, outgoing_.data(), count);
        outgoing_.erase(0, count);
        return count;
    }

    void write(std::string_view data) override {
        if (broken_)
            throw net::net_error("connection reset");
        transcript_ += data;
        incoming_ += data;
        while (true) {
            if (literal_left_ > 0) {
                const std::size_t take =
                    std::min(literal_left_, incoming_.size());
                literal_ += incoming_.substr(0, take);
                incoming_.erase(0, take);
                literal_left_ -= take;
                if (literal_left_ > 0)
                    return;
                on_literal(literal_);
                literal_.clear();
                continue;
            }
            const std::size_t end = incoming_.find("\r\n");
            if (end == std::string::npos)
                return;
            const std::string line = incoming_.substr(0, end);
            incoming_.erase(0, end + 2);
            on_line(line);
        }
    }

    void start_tls() override {
        tls_ = true;
        // A correct client discards this; a broken one would obey it.
        outgoing_.clear();
    }

    bool encrypted() const override { return tls_; }

    // Everything the client sent.
    std::string transcript_;
    // The next write fails, as a dead connection does.
    bool broken_ = false;

protected:
    virtual void on_line(const std::string &line) = 0;
    virtual void on_literal(const std::string &) {}

    void say(const std::string &text) { outgoing_ += text; }
    void expect_literal(std::size_t size) { literal_left_ = size; }

    std::string outgoing_;
    bool closed_ = false;
    bool tls_ = false;

private:
    std::string incoming_;
    std::string literal_;
    std::size_t literal_left_ = 0;
};

// Wraps a borrowed fake so a connector can hand it out as owned.
class borrowed_transport final : public net::transport {
public:
    explicit borrowed_transport(net::transport &inner) : inner_(inner) {}
    std::size_t read(char *b, std::size_t s) override {
        return inner_.read(b, s);
    }
    void write(std::string_view d) override { inner_.write(d); }
    void start_tls() override { inner_.start_tls(); }
    bool encrypted() const override { return inner_.encrypted(); }

private:
    net::transport &inner_;
};

// Returns a connector always handing out one transport.
inline net::connector connect_to(net::transport &server) {
    return [&server](const net::endpoint &) {
        return std::make_unique<borrowed_transport>(server);
    };
}

// An SMTP server that records what it receives.
class fake_smtp : public fake_transport {
public:
    fake_smtp() { say("220 test ESMTP\r\n"); }

    bool offer_starttls = true;
    bool inject_after_starttls = false;
    bool reject_login = false;
    std::string auth = "PLAIN LOGIN";
    std::vector<std::string> recipients;
    std::string sender;
    std::string message;
    std::set<std::string> refuse;

protected:
    void on_line(const std::string &line) override {
        if (in_data_) {
            if (line == ".") {
                in_data_ = false;
                say("250 queued\r\n");
            } else {
                message += line + "\r\n";
            }
            return;
        }
        if (login_step_ > 0) {
            --login_step_;
            say(login_step_ > 0 ? "334 UGFzc3dvcmQ6\r\n"
                                : "235 welcome\r\n");
            return;
        }
        if (util::istarts_with(line, "EHLO")) {
            say("250-test\r\n");
            if (offer_starttls && !tls_)
                say("250-STARTTLS\r\n");
            say("250 AUTH " + auth + "\r\n");
        } else if (line == "STARTTLS") {
            say("220 go\r\n");
            if (inject_after_starttls)
                say("250 injected\r\n");
        } else if (line.starts_with("AUTH PLAIN")) {
            say(reject_login ? "535 no\r\n" : "235 welcome\r\n");
        } else if (line == "AUTH LOGIN") {
            login_step_ = 2;
            say("334 VXNlcm5hbWU6\r\n");
        } else if (line == "AUTH CRAM-MD5") {
            say("334 PDE4OTYuNjk3MTcwOTUyQHBvc3RvZmZpY2UucmVzdG9uLm1jaS5uZX"
                "Q+\r\n");
            login_step_ = 1;
        } else if (line.starts_with("MAIL FROM:")) {
            sender = line.substr(11, line.size() - 12);
            say("250 ok\r\n");
        } else if (line.starts_with("RCPT TO:")) {
            const std::string who = line.substr(9, line.size() - 10);
            if (refuse.contains(who)) {
                say("550 no such user\r\n");
            } else {
                recipients.push_back(who);
                say("250 ok\r\n");
            }
        } else if (line == "DATA") {
            in_data_ = true;
            say("354 go\r\n");
        } else if (line == "QUIT") {
            say("221 bye\r\n");
            closed_ = true;
        } else {
            say("500 what\r\n");
        }
    }

private:
    bool in_data_ = false;
    int login_step_ = 0;
};

// A POP3 server over a list of messages.
class fake_pop3 : public fake_transport {
public:
    explicit fake_pop3(std::vector<std::string> messages)
        : messages(std::move(messages)) {
        say("+OK ready <1.2@test>\r\n");
    }

    std::vector<std::string> messages;
    std::set<int> deleted;
    bool committed = false;
    std::string password = "secret";
    std::string last_apop;

protected:
    void on_line(const std::string &line) override {
        if (line.starts_with("USER ")) {
            say("+OK\r\n");
        } else if (line.starts_with("PASS ")) {
            say(line.substr(5) == password ? "+OK\r\n" : "-ERR denied\r\n");
        } else if (line.starts_with("APOP ")) {
            last_apop = line;
            say("+OK\r\n");
        } else if (line == "STLS") {
            say("+OK\r\n");
        } else if (line == "LIST") {
            say("+OK\r\n");
            for (std::size_t i = 0; i < messages.size(); ++i)
                say(std::to_string(i + 1) + " " +
                    std::to_string(messages[i].size()) + "\r\n");
            say(".\r\n");
        } else if (line == "UIDL") {
            say("+OK\r\n");
            for (std::size_t i = 0; i < messages.size(); ++i)
                say(std::to_string(i + 1) + " uid" + std::to_string(i + 1) +
                    "\r\n");
            say(".\r\n");
        } else if (line.starts_with("RETR ")) {
            const auto index = util::parse_int(line.substr(5)).value_or(0);
            say("+OK\r\n");
            std::string text = messages[static_cast<std::size_t>(index - 1)];
            for (const std::string &part : util::split_lines(text))
                say((part.starts_with('.') ? "." : "") + part + "\r\n");
            say(".\r\n");
        } else if (line.starts_with("DELE ")) {
            deleted.insert(static_cast<int>(
                util::parse_int(line.substr(5)).value_or(0)));
            say("+OK\r\n");
        } else if (line == "QUIT") {
            committed = true;
            say("+OK bye\r\n");
            closed_ = true;
        } else {
            say("-ERR unknown\r\n");
        }
    }
};

// One mailbox of the fake IMAP server.
struct fake_mailbox {
    std::uint32_t uid_validity = 100;
    std::uint32_t next_uid = 1;
    std::map<std::uint32_t, std::string> messages;
    std::map<std::uint32_t, std::set<std::string>> flags;

    // Store a message; returns its UID.
    std::uint32_t put(const std::string &text,
                      std::set<std::string> message_flags = {}) {
        const std::uint32_t uid = next_uid++;
        messages[uid] = text;
        flags[uid] = std::move(message_flags);
        return uid;
    }
};

// An IMAP server implementing what imap_client uses.
class fake_imap : public fake_transport {
public:
    fake_imap() { say("* OK test ready\r\n"); }

    std::map<std::string, fake_mailbox> boxes;
    std::string password = "secret";
    std::string capabilities = "IMAP4rev1 UIDPLUS";
    int logins = 0;
    int full_fetches = 0;

    // Forget session state, as a new connection does.
    void reconnect() {
        outgoing_.clear();
        broken_ = false;
        closed_ = false;
        selected_.clear();
        say("* OK test ready\r\n");
    }

protected:
    void on_literal(const std::string &text) override {
        boxes[append_box_].put(text, append_flags_);
        const std::uint32_t uid = boxes[append_box_].next_uid - 1;
        append_pending_ = true;
        append_uid_ = uid;
    }

    void on_line(const std::string &line) override {
        if (append_pending_) {
            append_pending_ = false;
            say(append_tag_ + " OK [APPENDUID " +
                std::to_string(boxes[append_box_].uid_validity) + " " +
                std::to_string(append_uid_) + "] done\r\n");
            return;
        }
        const std::size_t space = line.find(' ');
        const std::string tag = line.substr(0, space);
        const std::string command = line.substr(space + 1);
        const auto ok = [&](const std::string &text = "done") {
            say(tag + " OK " + text + "\r\n");
        };
        if (command == "CAPABILITY") {
            say("* CAPABILITY " + capabilities + "\r\n");
            ok();
        } else if (command.starts_with("LOGIN ")) {
            ++logins;
            if (command.find(password) == std::string::npos)
                say(tag + " NO denied\r\n");
            else
                ok();
        } else if (command.starts_with("LIST ") ||
                   command.starts_with("LSUB ")) {
            for (const auto &[name, box] : boxes)
                say("* " + command.substr(0, 4) + " () \"/\" \"" + name +
                    "\"\r\n");
            ok();
        } else if (command.starts_with("SELECT ")) {
            selected_ = unquote(command.substr(7));
            const auto it = boxes.find(selected_);
            if (it == boxes.end()) {
                say(tag + " NO no such mailbox\r\n");
                return;
            }
            say("* " + std::to_string(it->second.messages.size()) +
                " EXISTS\r\n* OK [UIDVALIDITY " +
                std::to_string(it->second.uid_validity) + "] ok\r\n* OK "
                "[UIDNEXT " + std::to_string(it->second.next_uid) +
                "] ok\r\n");
            ok("[READ-WRITE] selected");
        } else if (command == "UID SEARCH ALL") {
            std::string reply = "* SEARCH";
            for (const auto &[uid, text] : boxes[selected_].messages)
                reply += " " + std::to_string(uid);
            say(reply + "\r\n");
            ok();
        } else if (command.starts_with("UID FETCH ")) {
            fetch(command.substr(10));
            ok();
        } else if (command.starts_with("UID STORE ")) {
            store(command.substr(10));
            ok();
        } else if (command.starts_with("UID EXPUNGE ")) {
            expunge();
            ok();
        } else if (command.starts_with("UID COPY ")) {
            copy(tag, command.substr(9));
        } else if (command.starts_with("APPEND ")) {
            append(tag, command.substr(7));
        } else if (command.starts_with("CREATE ")) {
            boxes[unquote(command.substr(7))];
            ok();
        } else if (command.starts_with("DELETE ")) {
            boxes.erase(unquote(command.substr(7)));
            ok();
        } else if (command.starts_with("RENAME ")) {
            const auto words = quoted_words(command.substr(7));
            auto node = boxes.extract(words[0]);
            node.key() = words[1];
            boxes.insert(std::move(node));
            ok();
        } else if (command == "NOOP") {
            ok();
        } else if (command == "LOGOUT") {
            say("* BYE\r\n");
            ok();
            closed_ = true;
        } else {
            say(tag + " BAD unknown\r\n");
        }
    }

private:
    static std::string unquote(const std::string &text) {
        return text.size() >= 2 ? text.substr(1, text.size() - 2) : text;
    }

    static std::vector<std::string> quoted_words(const std::string &text) {
        std::vector<std::string> words;
        std::size_t at = 0;
        while ((at = text.find('"', at)) != std::string::npos) {
            const std::size_t end = text.find('"', at + 1);
            words.push_back(text.substr(at + 1, end - at - 1));
            at = end + 1;
        }
        return words;
    }

    void fetch(const std::string &arguments) {
        const std::size_t space = arguments.find(' ');
        const std::string set = arguments.substr(0, space);
        const bool want_headers =
            arguments.find("BODY.PEEK[HEADER]") != std::string::npos;
        const bool want_body =
            arguments.find("BODY.PEEK[]") != std::string::npos;
        fake_mailbox &box = boxes[selected_];
        std::set<std::uint32_t> wanted;
        if (set != "1:*")
            for (const std::string &word : util::split(set, ','))
                wanted.insert(static_cast<std::uint32_t>(
                    util::parse_int(word).value_or(0)));
        int position = 0;
        for (const auto &[uid, text] : box.messages) {
            ++position;
            if (set != "1:*" && !wanted.contains(uid))
                continue;
            std::string flags;
            for (const std::string &flag : box.flags[uid])
                flags += (flags.empty() ? "" : " ") + flag;
            std::string reply = "* " + std::to_string(position) +
                                " FETCH (UID " + std::to_string(uid);
            if (!want_body)
                reply += " FLAGS (" + flags + ") RFC822.SIZE " +
                         std::to_string(text.size());
            if (want_headers) {
                const std::size_t end = text.find("\r\n\r\n");
                const std::string head =
                    text.substr(0, end == std::string::npos ? text.size()
                                                            : end + 4);
                reply += " BODY[HEADER] {" + std::to_string(head.size()) +
                         "}\r\n" + head;
            }
            if (want_body) {
                ++full_fetches;
                reply += " BODY[] {" + std::to_string(text.size()) +
                         "}\r\n" + text;
            }
            say(reply + ")\r\n");
        }
    }

    void store(const std::string &arguments) {
        const auto words = util::split(arguments, ' ');
        const auto uid = static_cast<std::uint32_t>(
            util::parse_int(words[0]).value_or(0));
        const bool add = words[1][0] == '+';
        const std::size_t open = arguments.find('(');
        const std::size_t close = arguments.find(')');
        for (const std::string &flag :
             util::split(arguments.substr(open + 1, close - open - 1), ' ')) {
            if (add)
                boxes[selected_].flags[uid].insert(flag);
            else
                boxes[selected_].flags[uid].erase(flag);
        }
    }

    void expunge() {
        fake_mailbox &box = boxes[selected_];
        for (auto it = box.messages.begin(); it != box.messages.end();) {
            if (box.flags[it->first].contains("\\Deleted"))
                it = box.messages.erase(it);
            else
                ++it;
        }
    }

    void copy(const std::string &tag, const std::string &arguments) {
        const auto words = util::split(arguments, ' ');
        const auto uid = static_cast<std::uint32_t>(
            util::parse_int(words[0]).value_or(0));
        const std::string target = unquote(words[1]);
        fake_mailbox &from = boxes[selected_];
        const std::uint32_t added =
            boxes[target].put(from.messages[uid], from.flags[uid]);
        say(tag + " OK [COPYUID 1 " + std::to_string(uid) + " " +
            std::to_string(added) + "] done\r\n");
    }

    void append(const std::string &tag, const std::string &arguments) {
        const auto words = quoted_words(arguments);
        append_box_ = words[0];
        append_tag_ = tag;
        append_flags_.clear();
        const std::size_t open = arguments.find('(');
        const std::size_t close = arguments.find(')');
        for (const std::string &flag :
             util::split(arguments.substr(open + 1, close - open - 1), ' '))
            if (!flag.empty())
                append_flags_.insert(flag);
        const std::size_t brace = arguments.rfind('{');
        expect_literal(static_cast<std::size_t>(
            util::parse_int(std::string_view(arguments)
                                .substr(brace + 1,
                                        arguments.size() - brace - 2))
                .value_or(0)));
        say("+ go\r\n");
    }

    std::string selected_;
    std::string append_box_;
    std::string append_tag_;
    std::set<std::string> append_flags_;
    bool append_pending_ = false;
    std::uint32_t append_uid_ = 0;
};

} // namespace testing

#endif
