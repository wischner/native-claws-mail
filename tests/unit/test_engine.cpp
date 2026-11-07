//
// The engine end to end, on the inline executor and in-memory servers:
// composing through the queue to SMTP and the sent folder, collecting
// over POP3 and IMAP with filters, failures, drafts and mbox.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <filesystem>
#include <unistd.h>

#include "app/engine.h"
#include "check.h"
#include "fake_server.h"
#include "mail/mbox.h"
#include "mime/part.h"
#include "util/files.h"

using namespace testing;
namespace fs = std::filesystem;

namespace {

// Records what the engine reports.
class recorder final : public app::engine_listener {
public:
    std::vector<app::folder_node> tree;
    app::folder_view shown;
    std::vector<std::string> failures;
    std::string last_status;
    int received = -1;

    void folders_changed(const std::vector<app::folder_node> &value) override {
        tree = value;
    }
    void folder_loaded(const app::folder_view &view) override { shown = view; }
    void status(const std::string &text) override { last_status = text; }
    void failure(const std::string &text) override {
        failures.push_back(text);
    }
    void mail_received(int count) override { received = count; }

    // Return a node by id, or null.
    const app::folder_node *find(const std::string &id) const {
        const auto walk = [&](const auto &self,
                              const app::folder_node &node)
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

// Hands out one fake per port, reconnecting it each time.
struct server_room {
    fake_smtp *smtp = nullptr;
    fake_pop3 *pop3 = nullptr;
    fake_imap *imap = nullptr;
    int smtp_failures = 0;

    net::connector connector() {
        return [this](const net::endpoint &target)
                   -> std::unique_ptr<net::transport> {
            if (target.port == 25) {
                if (smtp_failures > 0) {
                    --smtp_failures;
                    throw net::net_error("Cannot connect.");
                }
                return std::make_unique<borrowed_transport>(*smtp);
            }
            if (target.port == 110)
                return std::make_unique<borrowed_transport>(*pop3);
            imap->reconnect();
            return std::make_unique<borrowed_transport>(*imap);
        };
    }
};

prefs::account smtp_account() {
    prefs::account item;
    item.account_name = "main";
    item.full_name = "Me";
    item.address = "me@x.org";
    item.kind = prefs::protocol::pop3;
    item.receive_server = "pop";
    item.receive_port = 110;
    item.receive_security = prefs::security::none;
    item.smtp_server = "smtp";
    item.smtp_port = 25;
    item.smtp_security = prefs::security::none;
    item.user_id = "me";
    return item;
}

std::string text_message(const std::string &subject) {
    return "From: a@x.org\r\nTo: me@x.org\r\nSubject: " + subject +
           "\r\nMessage-ID: <" + subject +
           "@x>\r\nDate: Thu, 13 Feb 1975 23:31:30 +0000\r\n\r\nBody\r\n";
}

void test_send_and_receive(const fs::path &base) {
    app::paths where{base / "cfg", base / "Mail"};
    app::inline_executor jobs;
    recorder listener;
    server_room room;
    fake_pop3 pop({text_message("one"), text_message("spam two")});
    room.pop3 = &pop;
    app::engine core(where, jobs, listener, room.connector());
    core.load();
    CHECK(listener.failures.empty());
    CHECK(listener.find("#mh/Mailbox/inbox") != nullptr);

    prefs::account_list accounts;
    const int id = accounts.add(smtp_account());
    accounts.set_password(id, false, "secret");
    core.set_accounts(accounts);
    CHECK(core.accounts().find(id) != nullptr);

    std::vector<filter::rule> rules = {*filter::parse_rule(
        "subject matchcase \"spam\" move \"#mh/Mailbox/trash\"")};
    core.set_rules(rules);

    // Collect: filtered mail lands in the trash and the server is cleared.
    core.receive_all();
    CHECK(listener.failures.empty());
    CHECK(listener.received == 2);
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);
    CHECK(listener.find("#mh/Mailbox/trash")->total_count == 1);
    CHECK(pop.deleted.size() == 2);
    CHECK(pop.committed);

    // Open and read.
    core.open_folder("#mh/Mailbox/inbox");
    CHECK(listener.shown.messages.size() == 1);
    CHECK(listener.shown.messages[0].subject == "one");
    const std::uint32_t number = listener.shown.messages[0].number;
    std::optional<std::string> raw;
    core.fetch_message("#mh/Mailbox/inbox", number,
                       [&](auto text) { raw = std::move(text); });
    CHECK(raw.has_value());
    CHECK(raw->find("Subject: one") != std::string::npos);
    core.change_flags("#mh/Mailbox/inbox", {number}, 0,
                      mail::flag_new | mail::flag_unread);
    CHECK(listener.find("#mh/Mailbox/inbox")->unread_count == 0);

    // Reply and send: the original gets flagged, the copy lands in sent.
    compose::draft reply = compose::reply_to(
        *raw, smtp_account(), prefs::common{}, compose::reply_mode::sender);
    reply.account_id = id;
    reply.bcc = "hidden@x.org";
    reply.reply_source =
        "#mh/Mailbox/inbox\t" + std::to_string(number);
    fake_smtp smtp;
    room.smtp = &smtp;
    core.send(reply);
    CHECK(listener.failures.empty());
    CHECK(smtp.sender == "me@x.org");
    CHECK(smtp.recipients ==
          (std::vector<std::string>{"a@x.org", "hidden@x.org"}));
    CHECK(smtp.message.find("hidden") == std::string::npos);
    CHECK(smtp.message.find("Subject: Re: one") != std::string::npos);
    CHECK(smtp.transcript_.find("AUTH PLAIN") != std::string::npos);
    CHECK(listener.find("#mh/Mailbox/queue")->total_count == 0);
    CHECK(listener.find("#mh/Mailbox/sent")->total_count == 1);
    CHECK(listener.find("#mh/Mailbox/sent")->unread_count == 0);
    core.open_folder("#mh/Mailbox/sent");
    CHECK(listener.shown.messages[0].subject == "Re: one");
    core.open_folder("#mh/Mailbox/inbox");
    CHECK(listener.shown.messages[0].has(mail::flag_replied));

    // A failed send keeps the message queued for a retry.
    fake_smtp second;
    room.smtp = &second;
    room.smtp_failures = 1;
    compose::draft again = compose::new_draft(smtp_account());
    again.account_id = id;
    again.to = "b@x.org";
    again.subject = "Retry";
    again.body = "text";
    listener.failures.clear();
    core.send(again);
    CHECK(listener.failures.size() == 1);
    CHECK(listener.find("#mh/Mailbox/queue")->total_count == 1);
    core.send_queue();
    CHECK(second.recipients.size() == 1);
    CHECK(listener.find("#mh/Mailbox/queue")->total_count == 0);

    // No recipients is an error, and nothing is queued.
    listener.failures.clear();
    compose::draft empty = compose::new_draft(smtp_account());
    empty.account_id = id;
    core.send(empty);
    CHECK(listener.failures.size() == 1);
    CHECK(listener.find("#mh/Mailbox/queue")->total_count == 0);

    // Drafts keep what the wire version must not.
    compose::draft saved = compose::new_draft(smtp_account());
    saved.account_id = id;
    saved.to = "c@x.org";
    saved.bcc = "secret@x.org";
    saved.subject = "Later";
    saved.body = "not finished";
    core.save_draft(saved);
    CHECK(listener.find("#mh/Mailbox/draft")->total_count == 1);
    core.open_folder("#mh/Mailbox/draft");
    const std::uint32_t draft_number = listener.shown.messages[0].number;
    core.fetch_message("#mh/Mailbox/draft", draft_number,
                       [&](auto text) { raw = std::move(text); });
    const compose::draft back = compose::reopen(*raw);
    CHECK(back.account_id == id);
    CHECK(back.bcc == "secret@x.org");
    CHECK(back.body == "not finished");

    // Sending a reopened draft uses it up; so does saving over it.
    compose::draft resumed = back;
    resumed.to = "c@x.org";
    resumed.replaces = "#mh/Mailbox/draft\t" + std::to_string(draft_number);
    core.save_draft(resumed);
    CHECK(listener.find("#mh/Mailbox/draft")->total_count == 1);
    core.open_folder("#mh/Mailbox/draft");
    resumed.replaces = "#mh/Mailbox/draft\t" +
                       std::to_string(listener.shown.messages[0].number);
    fake_smtp third;
    room.smtp = &third;
    core.send(resumed);
    CHECK(listener.find("#mh/Mailbox/draft")->total_count == 0);
    CHECK(third.recipients ==
          (std::vector<std::string>{"c@x.org", "secret@x.org"}));

    // A queued message reopens with its routing.
    core.queue(saved);
    core.open_folder("#mh/Mailbox/queue");
    core.fetch_message("#mh/Mailbox/queue",
                       listener.shown.messages[0].number,
                       [&](auto text) { raw = std::move(text); });
    const compose::draft queued = compose::reopen(*raw);
    CHECK(queued.account_id == id);
    CHECK(queued.to == "c@x.org");
    CHECK(queued.bcc == "secret@x.org");
    CHECK(queued.body == "not finished");
    core.delete_messages("#mh/Mailbox/queue",
                         {listener.shown.messages[0].number});

    // Searching text in the store, not just summaries.
    std::set<std::uint32_t> found;
    core.search("#mh/Mailbox/inbox",
                *filter::parse_quick_search("b Body"),
                [&](std::set<std::uint32_t> hits) { found = std::move(hits); });
    CHECK(found.size() == 1);
    core.search("#mh/Mailbox/inbox",
                *filter::parse_quick_search("b zebra"),
                [&](std::set<std::uint32_t> hits) { found = std::move(hits); });
    CHECK(found.empty());

    // Folders.
    core.create_folder("#mh/Mailbox", "work");
    CHECK(listener.find("#mh/Mailbox/work") != nullptr);
    core.rename_folder("#mh/Mailbox/work", "job");
    CHECK(listener.find("#mh/Mailbox/job") != nullptr);
    listener.failures.clear();
    core.rename_folder("#mh/Mailbox/inbox", "x");
    CHECK(listener.failures.size() == 1);
    core.remove_folder("#mh/Mailbox/inbox");
    CHECK(listener.failures.size() == 2);
    CHECK(fs::exists(base / "Mail/inbox"));

    // Moving, deleting, the trash and locks.
    core.transfer("#mh/Mailbox/inbox", {number}, "#mh/Mailbox/job", true);
    CHECK(listener.find("#mh/Mailbox/job")->total_count == 1);
    core.change_flags("#mh/Mailbox/inbox", {number}, mail::flag_locked, 0);
    core.delete_messages("#mh/Mailbox/inbox", {number});
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);
    core.change_flags("#mh/Mailbox/inbox", {number}, 0, mail::flag_locked);
    core.delete_messages("#mh/Mailbox/inbox", {number});
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 0);
    CHECK(listener.find("#mh/Mailbox/trash")->total_count == 3);
    core.empty_trash();
    CHECK(listener.find("#mh/Mailbox/trash")->total_count == 0);

    // Mbox out and back.
    core.export_mbox("#mh/Mailbox/job", base / "out.mbox");
    CHECK(mail::split_mbox(*util::read_file(base / "out.mbox")).size() == 1);
    core.import_mbox(base / "out.mbox", "#mh/Mailbox/inbox");
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);

    // Everything survives a restart.
    core.shut_down();
    recorder fresh;
    app::engine reopened(where, jobs, fresh, room.connector());
    reopened.load();
    CHECK(fresh.find("#mh/Mailbox/job") != nullptr);
    CHECK(reopened.accounts().password(id, false) == "secret");
    CHECK(reopened.rules().size() == 1);
}

void test_leave_on_server(const fs::path &base) {
    app::paths where{base / "cfg2", base / "Mail2"};
    app::inline_executor jobs;
    recorder listener;
    server_room room;
    fake_pop3 pop({text_message("kept")});
    room.pop3 = &pop;
    app::engine core(where, jobs, listener, room.connector());
    core.load();
    prefs::account_list accounts;
    prefs::account item = smtp_account();
    item.remove_mail = false;
    const int id = accounts.add(item);
    accounts.set_password(id, false, "secret");
    core.set_accounts(accounts);
    core.receive_all();
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);
    fake_pop3 again({text_message("kept")});
    room.pop3 = &again;
    core.receive_all();
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);
    CHECK(pop.deleted.empty());

    // A wrong password reports, and collects nothing.
    fake_pop3 refuse({text_message("x")});
    refuse.password = "other";
    room.pop3 = &refuse;
    core.receive_all();
    CHECK(listener.failures.size() == 1);
    CHECK(listener.find("#mh/Mailbox/inbox")->total_count == 1);
}

void test_imap_account(const fs::path &base) {
    app::paths where{base / "cfg3", base / "Mail3"};
    app::inline_executor jobs;
    recorder listener;
    server_room room;
    fake_imap imap;
    imap.boxes["INBOX"].put(text_message("remote"));
    imap.boxes["Sent"];
    imap.boxes["Trash"];
    room.imap = &imap;
    app::engine core(where, jobs, listener, room.connector());
    core.load();
    prefs::account_list accounts;
    prefs::account item = smtp_account();
    item.account_name = "work";
    item.kind = prefs::protocol::imap4;
    item.receive_port = 143;
    item.imap_subscribed_only = false;
    const int id = accounts.add(item);
    accounts.set_password(id, false, "secret");
    core.set_accounts(accounts);
    CHECK(listener.failures.empty());
    CHECK(listener.find("#imap/work/INBOX") != nullptr);
    core.open_folder("#imap/work/INBOX");
    CHECK(listener.shown.messages.size() == 1);
    CHECK(listener.find("#imap/work/INBOX")->unread_count == 1);
    imap.boxes["INBOX"].put(text_message("later"));
    core.receive_all();
    CHECK(listener.received == 1);
    CHECK(listener.find("#imap/work/INBOX")->total_count == 2);

    // Sent mail goes to the account's own Sent folder.
    fake_smtp smtp;
    room.smtp = &smtp;
    compose::draft text = compose::new_draft(item);
    text.account_id = id;
    text.to = "x@x.org";
    text.subject = "Via imap";
    text.body = "b";
    core.send(text);
    CHECK(listener.failures.empty());
    CHECK(imap.boxes["Sent"].messages.size() == 1);
    core.delete_messages("#imap/work/INBOX", {1});
    CHECK(imap.boxes["Trash"].messages.size() == 1);
    core.shut_down();

    // Reopening restores the store from folderlist.xml.
    recorder fresh;
    app::engine again(where, jobs, fresh, room.connector());
    again.load();
    CHECK(fresh.find("#imap/work/INBOX") != nullptr);

    // A server that is down is reported, and the local store still works.
    recorder down;
    server_room empty_room;
    app::engine offline(where, jobs, down,
                        [](const net::endpoint &)
                            -> std::unique_ptr<net::transport> {
                            throw net::net_error("Cannot connect.");
                        });
    offline.load();
    CHECK(!down.failures.empty());
    CHECK(down.find("#mh/Mailbox/inbox") != nullptr);
}

void test_import(const fs::path &base) {
    const fs::path claws = base / "claws";
    util::write_file(claws / "accountrc", "[Account: 1]\nname=Me\n");
    util::write_file(claws / "addrbook/addrbook--index.xml", "<x/>");
    CHECK(app::engine::import_claws(claws, base / "imported"));
    CHECK(fs::exists(base / "imported/accountrc"));
    CHECK(fs::exists(base / "imported/addrbook/addrbook--index.xml"));
    CHECK(!app::engine::import_claws(claws, base / "imported"));
    CHECK(!app::engine::import_claws(base / "none", base / "other"));
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() /
                          ("blaise-engine-" + std::to_string(getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    test_send_and_receive(base);
    test_leave_on_server(base);
    test_imap_account(base);
    test_import(base);
    fs::remove_all(base);
    return testing::summary("engine");
}
