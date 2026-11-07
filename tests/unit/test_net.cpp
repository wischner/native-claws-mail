//
// SMTP, POP3 and IMAP against the in-memory servers, the IMAP store, and
// one round trip over a real local socket.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <filesystem>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "fake_server.h"
#include "mail/imap_folder.h"
#include "net/imap_client.h"
#include "net/pop3_client.h"
#include "net/smtp_client.h"
#include "util/codec.h"

using namespace testing;
namespace fs = std::filesystem;

namespace {

net::endpoint plain_target() {
    net::endpoint target;
    target.host = "test";
    target.port = 1;
    return target;
}

net::endpoint starttls_target() {
    net::endpoint target = plain_target();
    target.tls = net::tls_mode::starttls;
    return target;
}

template <typename Error, typename Call>
bool throws(Call &&call) {
    try {
        call();
    } catch (const Error &) {
        return true;
    }
    return false;
}

void test_smtp() {
    CHECK(net::dot_stuff(".a\r\nb\r\n..c\r\n") == "..a\r\nb\r\n...c\r\n");
    {
        fake_smtp server;
        net::smtp_client client(plain_target(), connect_to(server), "me");
        client.authenticate({"user", "pw"});
        client.send("a@x", {"b@x", "c@x"},
                    "Subject: hi\r\n\r\n.dot\r\nend\r\n");
        client.quit();
        CHECK(server.sender == "a@x");
        CHECK(server.recipients.size() == 2);
        CHECK(server.message.find("\r\n..dot\r\n") != std::string::npos);
        CHECK(server.transcript_.find("AUTH PLAIN") != std::string::npos);
    }
    {
        fake_smtp server;
        server.auth = "LOGIN";
        net::smtp_client client(plain_target(), connect_to(server), "me");
        client.authenticate({"user", "pw"});
        CHECK(server.transcript_.find(util::base64_encode("pw")) !=
              std::string::npos);
    }
    {
        fake_smtp server;
        server.auth = "PLAIN CRAM-MD5";
        net::smtp_client client(plain_target(), connect_to(server), "me");
        client.authenticate({"tim", "tanstaaftanstaaf"});
        // RFC 2195's worked exchange.
        CHECK(server.transcript_.find(
                  "dGltIGI5MTNhNjAyYzdlZGE3YTQ5NWI0ZTZlNzMzNGQzODkw") !=
              std::string::npos);
    }
    {
        fake_smtp server;
        server.reject_login = true;
        net::smtp_client client(plain_target(), connect_to(server), "me");
        CHECK(throws<net::auth_error>(
            [&] { client.authenticate({"u", "p"}); }));
    }
    {
        // No silent downgrade.
        fake_smtp server;
        server.offer_starttls = false;
        CHECK(throws<net::net_error>([&] {
            net::smtp_client client(starttls_target(), connect_to(server),
                                    "me");
        }));
    }
    {
        // Plain text injected behind the 220 must not be read as TLS.
        fake_smtp server;
        server.inject_after_starttls = true;
        net::smtp_client client(starttls_target(), connect_to(server), "me");
        CHECK(server.encrypted());
        client.send("a@x", {"b@x"}, "x\r\n");
        CHECK(server.recipients.size() == 1);
    }
    {
        fake_smtp server;
        server.refuse.insert("bad@x");
        net::smtp_client client(plain_target(), connect_to(server), "me");
        CHECK(throws<net::net_error>(
            [&] { client.send("a@x", {"bad@x"}, "x\r\n"); }));
        CHECK(server.message.empty());
        CHECK(throws<net::net_error>(
            [&] { client.send("a@x\r\nRSET", {"b@x"}, "x\r\n"); }));
        CHECK(throws<net::net_error>(
            [&] { client.send("a@x", {}, "x\r\n"); }));
    }
}

void test_pop3() {
    {
        fake_pop3 server({"Subject: a\r\n\r\n.dot\r\n", "Subject: b\r\n\r\n"});
        net::pop3_client client(plain_target(), connect_to(server));
        client.login("u", "secret", false);
        const auto entries = client.list();
        CHECK(entries.size() == 2);
        CHECK(entries[1].uid == "uid2");
        CHECK(client.retrieve(1) == "Subject: a\r\n\r\n.dot\r\n");
        client.remove(1);
        client.quit();
        CHECK(server.deleted.contains(1));
        CHECK(server.committed);
    }
    {
        fake_pop3 server({});
        net::pop3_client client(plain_target(), connect_to(server));
        CHECK(throws<net::auth_error>(
            [&] { client.login("u", "wrong", false); }));
    }
    {
        fake_pop3 server({});
        net::pop3_client client(plain_target(), connect_to(server));
        client.login("u", "pw", true);
        CHECK(server.last_apop ==
              "APOP u " + util::md5_hex("<1.2@test>pw"));
    }
    {
        // After STLS the old timestamp is gone, so APOP is refused here
        // rather than computed over an unauthenticated value.
        fake_pop3 server({});
        net::pop3_client client(starttls_target(), connect_to(server));
        CHECK(throws<net::auth_error>(
            [&] { client.login("u", "pw", true); }));
    }
}

void test_imap_names() {
    CHECK(net::imap_utf7_decode("Entw&APw-rfe") == "Entw\xC3\xBC" "rfe");
    CHECK(net::imap_utf7_encode("Entw\xC3\xBC" "rfe") == "Entw&APw-rfe");
    CHECK(net::imap_utf7_encode("a&b") == "a&-b");
    CHECK(net::imap_utf7_decode("a&-b") == "a&b");
    const std::string mixed = "\xC4\x8C\xF0\x9F\x93\xA7 x/y";
    CHECK(net::imap_utf7_decode(net::imap_utf7_encode(mixed)) == mixed);
    CHECK(net::imap_quote("a\"b\\c") == "\"a\\\"b\\\\c\"");
}

std::string message_text(const std::string &subject) {
    return "From: a@x\r\nSubject: " + subject +
           "\r\nMessage-ID: <" + subject +
           "@x>\r\n\r\nbody of " + subject + "\r\n";
}

void test_imap_client() {
    fake_imap server;
    server.boxes["INBOX"].put(message_text("one"), {"\\Seen"});
    server.boxes["INBOX"].put(message_text("two"));
    net::imap_client client(plain_target(), connect_to(server));
    CHECK(!client.authenticated());
    client.login("u", "secret");
    CHECK(client.authenticated());
    CHECK(client.list("", "*", false).size() == 1);
    const auto selection = client.select("INBOX");
    CHECK(selection.exists == 2);
    CHECK(selection.uid_validity == 100);
    CHECK(client.search_all() == (std::vector<std::uint32_t>{1, 2}));
    const auto summaries = client.fetch_summaries("1:*", true);
    CHECK(summaries.size() == 2);
    CHECK(summaries[0].flags.contains("\\Seen"));
    CHECK(summaries[1].headers.find("Subject: two") != std::string::npos);
    CHECK(client.fetch_message(2).value() == message_text("two"));
    CHECK(!client.fetch_message(9).has_value());
    client.store_flags(2, true, "\\Flagged");
    CHECK(server.boxes["INBOX"].flags[2].contains("\\Flagged"));
    client.create("Work");
    CHECK(client.copy(1, "Work") == 1);
    CHECK(client.append("Work", "Subject: x\n\ny\n", "\\Seen") == 2);
    CHECK(server.boxes["Work"].messages[2] == "Subject: x\r\n\r\ny\r\n");
    client.logout();
    // Names cannot carry a second command.
    CHECK(throws<net::net_error>([&] {
        fake_imap other;
        net::imap_client again(plain_target(), connect_to(other));
        again.create("x\r\na1 DELETE INBOX");
    }));
    fake_imap refuse;
    net::imap_client denied(plain_target(), connect_to(refuse));
    CHECK(throws<net::auth_error>([&] { denied.login("u", "bad"); }));
    {
        fake_imap no_tls;
        CHECK(throws<net::net_error>([&] {
            net::imap_client strict(starttls_target(), connect_to(no_tls));
        }));
    }
}

void test_imap_store(const fs::path &cache) {
    fake_imap server;
    server.boxes["INBOX"].put(message_text("one"), {"\\Seen"});
    server.boxes["INBOX"].put(message_text("two"), {"\\Flagged"});
    server.boxes["Sent"];
    server.boxes["Trash"];
    server.boxes["a/b"];

    mail::imap_settings settings;
    settings.server = plain_target();
    settings.user = "u";
    settings.password = "secret";
    settings.connect = [&server](const net::endpoint &) {
        server.reconnect();
        return std::make_unique<borrowed_transport>(server);
    };
    mail::imap_folder store("work", settings, cache);
    store.scan_tree();
    mail::folder_item *inbox = store.find_special(mail::item_type::inbox);
    CHECK(inbox != nullptr);
    CHECK(store.find_special(mail::item_type::trash) != nullptr);
    CHECK(store.find_item("a/b") != nullptr);
    CHECK(store.find_item("a") != nullptr && store.find_item("a")->no_select);
    inbox->scan();
    CHECK(inbox->total_count == 2);
    CHECK(inbox->unread_count == 1);
    CHECK(inbox->find(2)->has(mail::flag_marked));
    CHECK(inbox->find(1)->subject == "one");

    CHECK(store.fetch(*inbox, 2).value() == message_text("two"));
    const int fetched = server.full_fetches;
    CHECK(store.fetch(*inbox, 2).value() == message_text("two"));
    CHECK(server.full_fetches == fetched); // Served from the cache.

    inbox->change_flags(2, 0, mail::flag_unread | mail::flag_new);
    CHECK(server.boxes["INBOX"].flags[2].contains("\\Seen"));

    // A change on the server arrives at the next scan.
    server.boxes["INBOX"].flags[1].erase("\\Seen");
    server.boxes["INBOX"].flags[1].insert("\\Flagged");
    inbox->scan();
    CHECK(inbox->find(1)->has(mail::flag_unread));
    CHECK(inbox->find(1)->has(mail::flag_marked));

    mail::folder_item *trash = store.find_special(mail::item_type::trash);
    CHECK(mail::transfer_messages(*inbox, *trash, {2}, false) == 1);
    CHECK(inbox->total_count == 1);
    CHECK(server.boxes["Trash"].messages.size() == 1);
    CHECK(server.boxes["INBOX"].messages.size() == 1);

    mail::folder_item *made = store.create_folder(store.root(), "Projects");
    CHECK(made != nullptr);
    CHECK(server.boxes.contains("Projects"));
    CHECK(store.rename_folder(*made, "Done"));
    CHECK(server.boxes.contains("Done"));
    CHECK(store.remove_folder(*made));
    CHECK(!server.boxes.contains("Done"));

    // A dropped connection reconnects once.
    server.broken_ = true;
    server.boxes["INBOX"].put(message_text("three"));
    inbox->scan();
    CHECK(inbox->total_count == 2);
    CHECK(server.logins >= 2);

    // New UIDVALIDITY: the cache is thrown away.
    server.boxes["INBOX"].uid_validity = 200;
    mail::imap_folder fresh("work2", settings, cache);
    fresh.scan_tree();
    mail::folder_item *again = fresh.find_special(mail::item_type::inbox);
    again->scan();
    CHECK(again->total_count == 2);
    CHECK(!fs::exists(cache / "INBOX/1"));
}

void test_real_socket() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(listener, reinterpret_cast<sockaddr *>(&address),
                 sizeof address) == 0);
    socklen_t length = sizeof address;
    ::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
    ::listen(listener, 1);
    std::jthread server([listener] {
        const int peer = ::accept(listener, nullptr, nullptr);
        const std::string_view reply = "+OK hello\r\n+OK\r\n+OK\r\n";
        (void)!::write(peer, reply.data(), reply.size());
        char buffer[256];
        (void)!::read(peer, buffer, sizeof buffer);
        ::close(peer);
    });
    net::endpoint target;
    target.host = "127.0.0.1";
    target.port = ntohs(address.sin_port);
    target.timeout_seconds = 5;
    net::pop3_client client(target, net::socket_connector());
    client.login("u", "p", false);
    server.join();
    ::close(listener);
    target.port = 1;
    CHECK(throws<net::net_error>([&] {
        net::pop3_client refused(target, net::socket_connector());
    }));
    target.host = "no-such-host.invalid";
    CHECK(throws<net::net_error>([&] {
        net::pop3_client unknown(target, net::socket_connector());
    }));
}

} // namespace

int main() {
    const fs::path cache = fs::temp_directory_path() /
                           ("blaise-net-" + std::to_string(getpid()));
    fs::remove_all(cache);
    test_smtp();
    test_pop3();
    test_imap_names();
    test_imap_client();
    test_imap_store(cache);
    test_real_socket();
    fs::remove_all(cache);
    return testing::summary("net");
}
