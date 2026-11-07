//
// The engine against real servers: Dovecot for IMAP and TLS, Mailpit for
// SMTP. scripts/test/live.sh starts both in Docker; the in-memory fakes
// are written by the same hand as the clients, so this is what catches a
// misread RFC. Needs the servers, so it is not part of make test.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>

#include "app/engine.h"
#include "check.h"
#include "mail/imap_folder.h"
#include "net/imap_client.h"
#include "net/smtp_client.h"

namespace fs = std::filesystem;

namespace {

constexpr int plain_imap = 31143;
constexpr int tls_imap = 31993;
constexpr int smtp_port = 1025;
constexpr const char *user = "tester";
constexpr const char *password = "blaise-test";
constexpr const char *host = "127.0.0.1";

// Records what the engine reports.
class recorder final : public app::engine_listener {
public:
    std::vector<app::folder_node> tree;
    app::folder_view shown;
    std::vector<std::string> failures;
    int received = -1;

    void folders_changed(const std::vector<app::folder_node> &value) override {
        tree = value;
    }
    void folder_loaded(const app::folder_view &view) override { shown = view; }
    void status(const std::string &) override {}
    void failure(const std::string &text) override {
        std::cerr << "engine: " << text << '\n';
        failures.push_back(text);
    }
    void mail_received(int count) override { received = count; }

    // Return a node by identifier, or null.
    const app::folder_node *find(const std::string &id) const {
        const auto walk = [&](const auto &self, const app::folder_node &node)
            -> const app::folder_node * {
            if (node.id == id)
                return &node;
            for (const app::folder_node &child : node.children)
                if (const auto *hit = self(self, child))
                    return hit;
            return nullptr;
        };
        for (const app::folder_node &node : tree)
            if (const auto *hit = walk(walk, node))
                return hit;
        return nullptr;
    }
};

// Return a client session, logged in over plain TLS-less IMAP upgrade.
net::endpoint server(int port, net::tls_mode tls, bool accept = true) {
    net::endpoint target;
    target.host = host;
    target.port = port;
    target.tls = tls;
    target.accept_invalid_certificate = accept;
    target.timeout_seconds = 10;
    return target;
}

template <typename Call>
bool refused(Call &&call) {
    try {
        call();
    } catch (const net::net_error &failure) {
        std::cout << "  refused as expected: " << failure.what() << '\n';
        return true;
    }
    return false;
}

void test_tls() {
    const net::connector connect = net::socket_connector();
    // Implicit TLS, then STARTTLS.
    net::imap_client wrapped(server(tls_imap, net::tls_mode::wrapped),
                             connect);
    wrapped.login(user, password);
    CHECK(wrapped.authenticated());
    wrapped.logout();
    net::imap_client upgraded(server(plain_imap, net::tls_mode::starttls),
                              connect);
    upgraded.login(user, password);
    CHECK(upgraded.authenticated());
    CHECK(!upgraded.list("", "*", false).empty());
    upgraded.logout();
    // The server's certificate is self-signed: checking must reject it.
    CHECK(refused([&] {
        net::imap_client strict(
            server(tls_imap, net::tls_mode::wrapped, false), connect);
    }));
    CHECK(refused([&] {
        net::imap_client strict(
            server(plain_imap, net::tls_mode::starttls, false), connect);
    }));
    // A wrong password is an auth_error, not a network one.
    net::imap_client wrong(server(tls_imap, net::tls_mode::wrapped), connect);
    bool denied = false;
    try {
        wrong.login(user, "not the password");
    } catch (const net::auth_error &) {
        denied = true;
    }
    CHECK(denied);
}

prefs::account live_account() {
    prefs::account item;
    item.account_name = "live";
    item.is_default = true;
    item.full_name = "Test Er";
    item.address = "tester@example.org";
    item.kind = prefs::protocol::imap4;
    item.receive_server = host;
    item.receive_port = plain_imap;
    item.receive_security = prefs::security::starttls;
    item.accept_invalid_certificate = true;
    item.imap_subscribed_only = false;
    item.user_id = user;
    item.smtp_server = host;
    item.smtp_port = smtp_port;
    item.smtp_security = prefs::security::none;
    item.use_smtp_auth = false;
    return item;
}

void test_store(const fs::path &base) {
    app::inline_executor jobs;
    recorder events;
    app::engine core({base / "cfg", base / "Mail"}, jobs, events);
    core.load();
    prefs::account_list accounts;
    const int id = accounts.add(live_account());
    accounts.set_password(id, false, password);
    core.set_accounts(accounts);
    CHECK(events.failures.empty());
    const std::string inbox = "#imap/live/INBOX";
    CHECK(events.find(inbox) != nullptr);
    CHECK(events.find("#imap/live/Sent")->type == mail::item_type::outbox);
    CHECK(events.find("#imap/live/Trash")->type == mail::item_type::trash);
    CHECK(events.find("#imap/live/Work/Sub") != nullptr);

    core.open_folder(inbox);
    CHECK(events.shown.messages.size() >= 2);
    const std::uint32_t first = events.shown.messages.front().number;
    std::optional<std::string> raw;
    core.fetch_message(inbox, first, [&](auto text) { raw = std::move(text); });
    CHECK(raw && raw->find("Hello from a real server") != std::string::npos);

    // Flags reach the server: a second session sees them.
    core.change_flags(inbox, {first}, mail::flag_marked,
                      mail::flag_unread | mail::flag_new);
    {
        net::imap_client other(server(tls_imap, net::tls_mode::wrapped),
                               net::socket_connector());
        other.login(user, password);
        other.select("INBOX");
        const auto seen = other.fetch_summaries(std::to_string(first), false);
        CHECK(seen.size() == 1);
        CHECK(seen[0].flags.contains("\\Seen"));
        CHECK(seen[0].flags.contains("\\Flagged"));
        // And a change made elsewhere arrives at the next scan.
        other.store_flags(first, false, "\\Seen");
        other.logout();
    }
    core.open_folder(inbox);
    const auto again = std::ranges::find(events.shown.messages, first,
                                         &mail::msg_info::number);
    CHECK(again != events.shown.messages.end() &&
          again->has(mail::flag_unread));

    // Folders, with a name that needs modified UTF-7.
    const std::string name = "Pro\xC5\xA1la po\xC5\xA1ta";
    core.create_folder("#imap/live", name);
    CHECK(events.failures.empty());
    CHECK(events.find("#imap/live/" + name) != nullptr);
    core.transfer(inbox, {first}, "#imap/live/" + name, true);
    core.open_folder("#imap/live/" + name);
    CHECK(events.shown.messages.size() == 1);
    core.rename_folder("#imap/live/" + name, "Renamed");
    CHECK(events.find("#imap/live/Renamed") != nullptr);
    core.remove_folder("#imap/live/Renamed");
    CHECK(events.find("#imap/live/Renamed") == nullptr);
    CHECK(events.failures.empty());

    // Send through a real SMTP server; the copy lands in IMAP Sent.
    compose::draft text = compose::new_draft(live_account());
    text.account_id = id;
    text.to = "Ana \xC4\x8C <ana@example.org>";
    text.cc = "carol@example.org";
    text.bcc = "hidden@example.org";
    text.subject = "Live caf\xC3\xA9 test";
    text.body = "Sent over a real socket.\n.leading dot\n\xC4\x8D\n";
    text.attachments.push_back({"", "note.txt", "text/plain", "attached"});
    const auto sent_before = events.find("#imap/live/Sent")->total_count;
    core.send(text);
    CHECK(events.failures.empty());
    core.open_folder("#imap/live/Sent");
    CHECK(static_cast<int>(events.shown.messages.size()) == sent_before + 1);
    // What went over the wire, as filed: Bcc is an envelope matter only.
    std::optional<std::string> filed;
    core.fetch_message("#imap/live/Sent",
                       events.shown.messages.back().number,
                       [&](auto bytes) { filed = std::move(bytes); });
    CHECK(filed.has_value());
    CHECK(filed && filed->find("hidden@example.org") == std::string::npos);
    CHECK(filed && filed->find("Subject: =?UTF-8?Q?Live") != std::string::npos);

    // Move to trash and delete for good.
    const std::size_t count_before = [&] {
        core.open_folder(inbox);
        return events.shown.messages.size();
    }();
    core.delete_messages(inbox, {first});
    CHECK(events.shown.messages.size() == count_before - 1);
    CHECK(events.find("#imap/live/Trash")->total_count >= 1);
    core.empty_trash();
    CHECK(events.find("#imap/live/Trash")->total_count == 0);
    CHECK(events.failures.empty());

    // Everything comes back after a restart.
    core.shut_down();
    recorder fresh;
    app::engine reopened({base / "cfg", base / "Mail"}, jobs, fresh);
    reopened.load();
    CHECK(fresh.failures.empty());
    CHECK(fresh.find(inbox) != nullptr);
    reopened.open_folder(inbox);
    CHECK(fresh.shown.messages.size() == count_before - 1);
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() /
                          ("blaise-live-" + std::to_string(getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    test_tls();
    test_store(base);
    fs::remove_all(base);
    return testing::summary("live");
}
