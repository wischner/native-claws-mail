//
// The engine's network work: collecting mail and sending it.
//
// Derived from Claws Mail src/inc.c, src/send_message.c and
// src/procmsg.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <exception>
#include <map>
#include <set>

#include "app/engine.h"
#include "compose/queue.h"
#include "mail/msg_info.h"
#include "mime/address.h"
#include "mime/header.h"
#include "net/pop3_client.h"
#include "net/smtp_client.h"
#include "util/files.h"
#include "util/log.h"
#include "util/text.h"

namespace app {
namespace {

namespace fs = std::filesystem;

// Return the transport security for a setting.
net::tls_mode tls_for(prefs::security value) {
    switch (value) {
    case prefs::security::tls:
        return net::tls_mode::wrapped;
    case prefs::security::starttls:
        return net::tls_mode::starttls;
    case prefs::security::none:
        break;
    }
    return net::tls_mode::none;
}

// Return the host name given to SMTP. The sender's domain says nothing
// the message does not already say; the machine's name would.
std::string hello_name(const prefs::account &item) {
    const std::size_t at = item.address.rfind('@');
    return at == std::string::npos ? "localhost"
                                   : item.address.substr(at + 1);
}

// Read the UIDs already collected.
std::set<std::string> read_uids(const fs::path &file) {
    std::set<std::string> uids;
    if (const auto text = util::read_file(file))
        for (const std::string &line : util::split_lines(*text))
            uids.insert(line.substr(0, line.find(' ')));
    return uids;
}

// Write the UIDs collected.
void write_uids(const fs::path &file, const std::set<std::string> &uids) {
    std::string out;
    const std::string stamp = " 0 " + std::to_string(std::time(nullptr));
    for (const std::string &uid : uids)
        out += uid + stamp + "\n";
    util::write_file(file, out);
}

} // namespace

fs::path engine::uid_file(const prefs::account &item) const {
    return paths_.config / "uidl" /
           (item.receive_server + "-" + item.user_id);
}

int engine::deliver_new(mail::folder_item &inbox, const prefs::account &item,
                        const std::vector<std::uint32_t> &numbers) {
    inbox.scan();
    if (item.filter_on_receive)
        filter::apply_rules(rules(), inbox, numbers, folders_, item.id);
    publish_folder(inbox);
    return static_cast<int>(numbers.size());
}

int engine::receive_pop3(const prefs::account &item) {
    mail::folder_item *inbox = target_for(item, item.inbox,
                                          mail::item_type::inbox);
    if (inbox == nullptr)
        throw std::runtime_error("No inbox for " + item.address);
    net::endpoint server;
    server.host = item.receive_server;
    server.port = item.effective_receive_port();
    server.tls = tls_for(item.receive_security);
    server.accept_invalid_certificate = item.accept_invalid_certificate;
    net::pop3_client client(server, connect_);
    client.login(item.user_id, accounts().password(item.id, false),
                 item.use_apop);
    const fs::path file = uid_file(item);
    std::set<std::string> seen = read_uids(file);
    std::set<std::string> present;
    std::vector<std::uint32_t> added;
    for (const net::pop3_entry &entry : client.list()) {
        // A server without UIDL has no way to know what was collected,
        // so everything is new; only remove_mail keeps that sane.
        present.insert(entry.uid);
        if (!entry.uid.empty() && seen.contains(entry.uid))
            continue;
        const std::string raw = client.retrieve(entry.number);
        const std::uint32_t number = inbox->owner().add(*inbox, raw, 0);
        if (number == 0)
            throw std::runtime_error("Could not store a received message.");
        added.push_back(number);
        seen.insert(entry.uid);
        // The copy is on disk, so the server's may now go.
        if (item.remove_mail) {
            client.remove(entry.number);
            seen.erase(entry.uid);
        }
    }
    client.quit();
    // Forget what the server no longer has, or the file grows forever.
    std::erase_if(seen, [&](const std::string &uid) {
        return !present.contains(uid);
    });
    write_uids(file, seen);
    return deliver_new(*inbox, item, added);
}

void engine::receive_one(const prefs::account &item, int &total) {
    switch (item.kind) {
    case prefs::protocol::pop3:
        total += receive_pop3(item);
        break;
    case prefs::protocol::imap4:
        for (const auto &store : folders_.all()) {
            if (store->type_name() != "imap" ||
                store->account_id() != item.id)
                continue;
            mail::folder_item *inbox =
                store->find_special(mail::item_type::inbox);
            if (inbox == nullptr)
                continue;
            const int before = inbox->total_count;
            std::set<std::uint32_t> known;
            for (const auto &[number, info] : inbox->messages())
                known.insert(number);
            inbox->scan();
            std::vector<std::uint32_t> fresh;
            for (const auto &[number, info] : inbox->messages())
                if (!known.contains(number))
                    fresh.push_back(number);
            if (before != 0 || !fresh.empty())
                total += deliver_new(*inbox, item, fresh);
        }
        break;
    default:
        break;
    }
}

void engine::receive_all() {
    submit([this] {
        int total = 0;
        const prefs::account_list list = accounts();
        for (const prefs::account &item : list.all()) {
            if (!item.receive_at_get_all ||
                item.kind == prefs::protocol::none)
                continue;
            try {
                receive_one(item, total);
            } catch (const std::exception &failure) {
                // One broken account must not stop the others.
                report(item.account_name + ": " + failure.what());
            }
        }
        save_folders();
        publish_tree();
        jobs_.deliver([this, total] { listener_.mail_received(total); });
    });
}

void engine::receive_account(int account_id) {
    submit([this, account_id] {
        int total = 0;
        const prefs::account_list list = accounts();
        const prefs::account *item = list.find(account_id);
        if (item == nullptr)
            return;
        receive_one(*item, total);
        save_folders();
        publish_tree();
        jobs_.deliver([this, total] { listener_.mail_received(total); });
    });
}

mail::folder_item *engine::target_for(const prefs::account &item,
                                      const std::string &chosen,
                                      mail::item_type type) {
    if (!chosen.empty())
        if (mail::folder_item *found = folders_.find(chosen))
            return found;
    for (const auto &store : folders_.all())
        if (store->account_id() == item.id)
            if (mail::folder_item *found = store->find_special(type))
                return found;
    return folders_.special_for(nullptr, type);
}

void engine::queue_sub(const compose::draft &text) {
    const prefs::account_list list = accounts();
    const prefs::account *item = list.find(text.account_id);
    if (item == nullptr)
        item = list.default_account();
    if (item == nullptr)
        throw std::runtime_error("There is no account to send from.");
    const auto outgoing = compose::to_outgoing(text, *item, preferences(),
                                               std::time(nullptr));
    if (!outgoing)
        throw std::runtime_error("An attachment could not be read.");
    compose::queued_message entry;
    entry.sender = item->address;
    entry.smtp_server = item->smtp_server;
    entry.recipients = text.recipients();
    entry.account_id = item->id;
    entry.reply_source = text.reply_source;
    entry.forward_source = text.forward_source;
    if (preferences().save_message_to_sent)
        if (mail::folder_item *sent = target_for(
                *item, item->sent_folder, mail::item_type::outbox))
            entry.sent_folder = sent->identifier();
    entry.message = mime::render_message(*outgoing);
    if (entry.recipients.empty())
        throw std::runtime_error("The message has no recipients.");
    mail::folder_item *queue =
        target_for(*item, item->queue_folder, mail::item_type::queue);
    if (queue == nullptr)
        throw std::runtime_error("There is no queue folder.");
    queue->owner().add(*queue, compose::write_queue_entry(entry), 0);
    queue->scan();
}

void engine::mark_source(const std::string &source, std::uint32_t flag) {
    const std::size_t tab = source.find('\t');
    if (tab == std::string::npos)
        return;
    mail::folder_item *item = folders_.find(source.substr(0, tab));
    const auto number = util::parse_int(source.substr(tab + 1));
    if (item != nullptr && number) {
        item->change_flags(static_cast<std::uint32_t>(*number), flag, 0);
        item->save();
        publish_folder(*item);
    }
}

void engine::send_one(mail::folder &store, mail::folder_item &queue,
                      std::uint32_t number,
                      const prefs::account_list &list) {
    const auto raw = store.fetch(queue, number);
    if (!raw)
        return;
    const compose::queued_message entry = compose::read_queue_entry(*raw);
    const prefs::account *item = list.find(entry.account_id);
    if (item == nullptr)
        item = list.default_account();
    if (item == nullptr || entry.recipients.empty())
        throw std::runtime_error(
            "A queued message has no account or recipients.");
    net::endpoint server;
    server.host = item->smtp_server;
    server.port = item->effective_smtp_port();
    server.tls = tls_for(item->smtp_security);
    server.accept_invalid_certificate = item->accept_invalid_certificate;
    net::smtp_client client(server, connect_, hello_name(*item));
    if (item->use_smtp_auth) {
        std::string password = list.password(item->id, true);
        if (password.empty())
            password = list.password(item->id, false);
        client.authenticate({item->effective_smtp_user(), password});
    }
    client.send(entry.sender, entry.recipients, entry.message);
    client.quit();
    // The mail has left. Filing failures below must not send it twice,
    // so the queue copy goes first.
    if (store.remove(queue, number))
        queue.forget(number);
    queue.save();
    if (mail::folder_item *target = folders_.find(entry.sent_folder)) {
        target->messages();
        const std::uint32_t added =
            target->owner().add(*target, entry.message, 0);
        if (added == 0)
            throw std::runtime_error("The sent message could not be filed.");
        target->scan();
        target->change_flags(added, 0, mail::flag_new | mail::flag_unread);
        target->save();
    }
    mark_source(entry.reply_source, mail::flag_replied);
    mark_source(entry.forward_source, mail::flag_forwarded);
}

void engine::send_queue_sub() {
    const prefs::account_list list = accounts();
    int sent = 0;
    for (const auto &store : folders_.all()) {
        mail::folder_item *queue =
            store->find_special(mail::item_type::queue);
        if (queue == nullptr)
            continue;
        queue->scan();
        std::vector<std::uint32_t> numbers;
        for (const auto &[number, info] : queue->messages())
            numbers.push_back(number);
        for (const std::uint32_t number : numbers) {
            // One bad message must not hold the rest of the queue back.
            try {
                send_one(*store, *queue, number, list);
                ++sent;
            } catch (const std::exception &failure) {
                report(failure.what());
            }
        }
    }
    publish_tree();
    const std::string text = std::to_string(sent) + " sent";
    jobs_.deliver([this, text] { listener_.status(text); });
}

void engine::remove_source(const std::string &source) {
    const std::size_t tab = source.find('\t');
    if (tab == std::string::npos)
        return;
    mail::folder_item *item = folders_.find(source.substr(0, tab));
    const auto number = util::parse_int(source.substr(tab + 1));
    if (item == nullptr || !number)
        return;
    const auto wanted = static_cast<std::uint32_t>(*number);
    if (item->owner().remove(*item, wanted))
        item->forget(wanted);
    item->save();
    publish_folder(*item);
}

void engine::queue(compose::draft text) {
    submit([this, text = std::move(text)] {
        queue_sub(text);
        remove_source(text.replaces);
        publish_tree();
    });
}

void engine::send(compose::draft text) {
    submit([this, text = std::move(text)] {
        queue_sub(text);
        // The new queue entry exists, so the old copy can go before a
        // send that might fail.
        remove_source(text.replaces);
        send_queue_sub();
    });
}

void engine::send_queue() {
    submit([this] { send_queue_sub(); });
}

void engine::save_draft(compose::draft text) {
    submit([this, text = std::move(text)] {
        const prefs::account_list list = accounts();
        const prefs::account *item = list.find(text.account_id);
        if (item == nullptr)
            item = list.default_account();
        if (item == nullptr)
            throw std::runtime_error("There is no account to save for.");
        const auto outgoing = compose::to_outgoing(
            text, *item, preferences(), std::time(nullptr));
        if (!outgoing)
            throw std::runtime_error("An attachment could not be read.");
        mime::outgoing_message message = *outgoing;
        message.extra_headers.push_back(
            {"X-Claws-Account-Id", std::to_string(item->id)});
        if (!text.bcc.empty())
            message.extra_headers.push_back({"Bcc", text.bcc});
        mail::folder_item *drafts =
            target_for(*item, item->draft_folder, mail::item_type::draft);
        if (drafts == nullptr)
            throw std::runtime_error("There is no drafts folder.");
        const std::uint32_t number = drafts->owner().add(
            *drafts, mime::render_message(message), 0);
        drafts->scan();
        drafts->change_flags(number, 0,
                             mail::flag_new | mail::flag_unread);
        drafts->save();
        remove_source(text.replaces);
        publish_tree();
        jobs_.deliver([this] { listener_.status("Draft saved"); });
    });
}

} // namespace app
