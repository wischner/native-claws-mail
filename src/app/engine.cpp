//
// The engine: opening stores, moving mail between them, and everything
// that needs no network.
//
// Derived from Claws Mail src/main.c and src/folder.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "app/engine.h"

#include <algorithm>
#include <cstdlib>
#include <exception>

#include "mail/imap_folder.h"
#include "mail/mh_folder.h"
#include "util/files.h"
#include "util/log.h"

namespace app {
namespace {

namespace fs = std::filesystem;

constexpr const char *folder_file = "folderlist.xml";
constexpr const char *rule_file = "matcherrc";

// Return a snapshot of an item and everything under it.
folder_node snapshot_item(const mail::folder_item &item) {
    folder_node node;
    node.id = item.identifier();
    node.name = item.name;
    node.type = item.type;
    node.no_select = item.no_select;
    node.collapsed = item.collapsed;
    node.new_count = item.new_count;
    node.unread_count = item.unread_count;
    node.total_count = item.total_count;
    for (const auto &child : item.children)
        node.children.push_back(snapshot_item(*child));
    return node;
}

// Return the IMAP settings of an account.
mail::imap_settings settings_for(const prefs::account &item,
                                 const prefs::account_list &accounts,
                                 const net::connector &connect) {
    mail::imap_settings settings;
    settings.server.host = item.receive_server;
    settings.server.port = item.effective_receive_port();
    settings.server.accept_invalid_certificate =
        item.accept_invalid_certificate;
    switch (item.receive_security) {
    case prefs::security::tls:
        settings.server.tls = net::tls_mode::wrapped;
        break;
    case prefs::security::starttls:
        settings.server.tls = net::tls_mode::starttls;
        break;
    case prefs::security::none:
        settings.server.tls = net::tls_mode::none;
        break;
    }
    settings.user = item.user_id;
    settings.password = accounts.password(item.id, false);
    settings.directory = item.imap_directory;
    settings.subscribed_only = item.imap_subscribed_only;
    settings.connect = connect;
    return settings;
}

} // namespace

paths paths::defaults() {
    const fs::path home = util::home_directory();
    return {home / ".blaise", home / "Mail"};
}

// Rebuilds the IMAP stores named in folderlist.xml.
class engine::imap_factory final : public mail::folder_factory {
public:
    explicit imap_factory(engine &owner) : owner_(owner) {}

    std::unique_ptr<mail::folder> create(const std::string &type,
                                         const std::string &name,
                                         int account_id,
                                         const fs::path &) override {
        const prefs::account *item = owner_.find_account(account_id);
        if (type != "imap" || item == nullptr)
            return nullptr;
        return owner_.make_imap(name, *item, account_id);
    }

private:
    engine &owner_;
};

engine::engine(paths where, executor &jobs, engine_listener &listener,
               net::connector connect)
    : paths_(std::move(where)), jobs_(jobs), listener_(listener),
      connect_(std::move(connect)) {}

void engine::submit(job work) {
    jobs_.run([this, work = std::move(work)] {
        try {
            work();
        } catch (const std::exception &failure) {
            report(failure.what());
        }
    });
}

void engine::report(const std::string &text) {
    util::log_write(util::log_level::error, text);
    jobs_.deliver([this, text] { listener_.failure(text); });
}

void engine::publish_tree() {
    std::vector<folder_node> tree;
    for (const auto &store : folders_.all()) {
        folder_node node = snapshot_item(store->root());
        node.is_store = true;
        node.name = store->name();
        tree.push_back(std::move(node));
    }
    jobs_.deliver([this, tree = std::move(tree)] {
        listener_.folders_changed(tree);
    });
}

void engine::publish_folder(mail::folder_item &item) {
    folder_view view;
    view.id = item.identifier();
    view.name = item.name;
    view.type = item.type;
    view.sort = item.sort;
    view.sort_ascending = item.sort_ascending;
    view.threaded = item.threaded;
    for (const auto &[number, info] : item.messages())
        view.messages.push_back(info);
    jobs_.deliver([this, view = std::move(view)] {
        listener_.folder_loaded(view);
    });
}

std::unique_ptr<mail::folder>
engine::make_imap(const std::string &name, const prefs::account &item,
                  int account_id) {
    const fs::path cache = paths_.config / "imapcache" /
                           (item.receive_server + "-" + item.user_id);
    auto store = std::make_unique<mail::imap_folder>(
        name, settings_for(item, accounts_, connect_), cache);
    store->set_account_id(account_id);
    return store;
}

void engine::sync_stores() {
    // Stores whose account has gone.
    std::vector<mail::folder *> orphans;
    for (const auto &store : folders_.all())
        if (store->type_name() == "imap" &&
            find_account(store->account_id()) == nullptr)
            orphans.push_back(store.get());
    for (mail::folder *store : orphans)
        folders_.remove(*store);
    // Accounts with no store yet, and fresh credentials for the others.
    for (const prefs::account &item : accounts_.all()) {
        if (item.kind != prefs::protocol::imap4)
            continue;
        bool found = false;
        for (const auto &store : folders_.all()) {
            if (store->type_name() != "imap" ||
                store->account_id() != item.id)
                continue;
            found = true;
            static_cast<mail::imap_folder &>(*store).update_settings(
                settings_for(item, accounts_, connect_));
        }
        if (!found)
            folders_.add(make_imap(
                item.account_name.empty() ? item.address : item.account_name,
                item, item.id));
    }
}

void engine::load() {
    submit([this] {
        {
            const std::scoped_lock hold(settings_lock_);
            accounts_.load(paths_.config);
            preferences_.load(paths_.config);
        }
        rules_.load(paths_.config / rule_file);
        imap_factory factory(*this);
        folders_.load(paths_.config / folder_file, &factory);
        const bool has_local = std::ranges::any_of(
            folders_.all(), [](const auto &store) {
                return store->type_name() == "mh";
            });
        if (!has_local)
            folders_.add(std::make_unique<mail::mh_folder>("Mailbox",
                                                           paths_.mail));
        sync_stores();
        publish_tree();
        // Remote trees follow; the local one is already on screen.
        for (const auto &store : folders_.all()) {
            if (store->type_name() != "imap")
                continue;
            try {
                store->scan_tree();
            } catch (const net::net_error &failure) {
                report(store->name() + ": " + failure.what());
            }
        }
        save_folders();
        publish_tree();
    });
}

void engine::save_folders() {
    folders_.save(paths_.config / folder_file);
}

void engine::shut_down() {
    submit([this] {
        if (preferences().empty_trash_on_exit)
            for (const auto &store : folders_.all())
                if (store->type_name() == "mh")
                    if (mail::folder_item *trash = store->find_special(
                            mail::item_type::trash)) {
                        std::vector<std::uint32_t> numbers;
                        for (const auto &[number, info] : trash->messages())
                            numbers.push_back(number);
                        for (const std::uint32_t number : numbers)
                            if (store->remove(*trash, number))
                                trash->forget(number);
                    }
        for (const auto &store : folders_.all())
            store->for_each([](mail::folder_item &item) { item.save(); });
        save_folders();
    });
}

prefs::account_list engine::accounts() const {
    const std::scoped_lock hold(settings_lock_);
    return accounts_;
}

void engine::set_accounts(prefs::account_list list) {
    submit([this, list = std::move(list)]() mutable {
        {
            const std::scoped_lock hold(settings_lock_);
            accounts_ = std::move(list);
            if (!accounts_.save(paths_.config))
                report("Could not save the accounts.");
        }
        sync_stores();
        for (const auto &store : folders_.all())
            if (store->type_name() == "imap" &&
                store->root().children.empty()) {
                try {
                    store->scan_tree();
                } catch (const net::net_error &failure) {
                    report(store->name() + ": " + failure.what());
                }
            }
        save_folders();
        publish_tree();
    });
}

prefs::common engine::preferences() const {
    const std::scoped_lock hold(settings_lock_);
    return preferences_;
}

void engine::set_preferences(prefs::common values) {
    submit([this, values = std::move(values)] {
        const std::scoped_lock hold(settings_lock_);
        preferences_ = values;
        if (!preferences_.save(paths_.config))
            report("Could not save the preferences.");
    });
}

std::vector<filter::rule> engine::rules() const {
    const std::scoped_lock hold(settings_lock_);
    return rules_.rules();
}

void engine::set_rules(std::vector<filter::rule> rules) {
    submit([this, rules = std::move(rules)]() mutable {
        const std::scoped_lock hold(settings_lock_);
        rules_.rules() = std::move(rules);
        if (!rules_.save(paths_.config / rule_file))
            report("Could not save the filter rules.");
    });
}

const prefs::account *engine::find_account(int id) const {
    return accounts_.find(id);
}

mail::folder_item *engine::require(const std::string &id) {
    mail::folder_item *item = folders_.find(id);
    if (item == nullptr)
        throw std::runtime_error("Folder not found: " + id);
    return item;
}

void engine::refresh_tree() {
    submit([this] {
        for (const auto &store : folders_.all())
            store->scan_tree();
        save_folders();
        publish_tree();
    });
}

void engine::open_folder(const std::string &id) {
    submit([this, id] {
        mail::folder_item *item = require(id);
        item->scan();
        publish_folder(*item);
        publish_tree();
    });
}

void engine::set_folder_view(const std::string &id, mail::sort_key sort,
                             bool ascending, bool threaded) {
    submit([this, id, sort, ascending, threaded] {
        mail::folder_item *item = require(id);
        item->sort = sort;
        item->sort_ascending = ascending;
        item->threaded = threaded;
    });
}

void engine::fetch_message(
    const std::string &id, std::uint32_t number,
    std::function<void(std::optional<std::string>)> done) {
    submit([this, id, number, done = std::move(done)] {
        mail::folder_item *item = require(id);
        auto raw = item->owner().fetch(*item, number);
        jobs_.deliver([done, raw = std::move(raw)] { done(raw); });
    });
}

void engine::search(const std::string &id, filter::condition_list conditions,
                    std::function<void(std::set<std::uint32_t>)> done) {
    submit([this, id, conditions = std::move(conditions),
            done = std::move(done)] {
        mail::folder_item *item = require(id);
        std::set<std::uint32_t> found;
        for (const auto &[number, info] : item->messages()) {
            const mail::msg_info &summary = info;
            const std::uint32_t wanted = number;
            if (conditions.matches(
                    {summary,
                     [&] { return item->owner().fetch(*item, wanted); },
                     0}))
                found.insert(number);
        }
        jobs_.deliver([done, found = std::move(found)] { done(found); });
    });
}

void engine::change_flags(const std::string &id,
                          std::vector<std::uint32_t> numbers,
                          std::uint32_t set, std::uint32_t clear) {
    submit([this, id, numbers = std::move(numbers), set, clear] {
        mail::folder_item *item = require(id);
        for (const std::uint32_t number : numbers)
            item->change_flags(number, set, clear);
        item->save();
        publish_tree();
    });
}

void engine::transfer(const std::string &from,
                      std::vector<std::uint32_t> numbers,
                      const std::string &to, bool keep) {
    submit([this, from, numbers = std::move(numbers), to, keep] {
        mail::folder_item *source = require(from);
        mail::folder_item *target = require(to);
        const int done =
            mail::transfer_messages(*source, *target, numbers, keep);
        publish_folder(*source);
        publish_tree();
        const std::string text = std::to_string(done) +
                                 (keep ? " copied to " : " moved to ") +
                                 target->name;
        jobs_.deliver([this, text] { listener_.status(text); });
    });
}

void engine::delete_messages(const std::string &id,
                             std::vector<std::uint32_t> numbers) {
    submit([this, id, numbers = std::move(numbers)] {
        mail::folder_item *item = require(id);
        mail::folder_item *trash =
            folders_.special_for(item, mail::item_type::trash);
        // Locked mail stays, as Claws has it.
        std::vector<std::uint32_t> removable;
        for (const std::uint32_t number : numbers)
            if (const mail::msg_info *info = item->find(number);
                info != nullptr && !info->has(mail::flag_locked))
                removable.push_back(number);
        if (trash == nullptr || item == trash ||
            item->type == mail::item_type::trash) {
            for (const std::uint32_t number : removable)
                if (item->owner().remove(*item, number))
                    item->forget(number);
            item->save();
        } else {
            mail::transfer_messages(*item, *trash, removable, false);
        }
        publish_folder(*item);
        publish_tree();
    });
}

void engine::empty_trash() {
    submit([this] {
        for (const auto &store : folders_.all()) {
            mail::folder_item *trash =
                store->find_special(mail::item_type::trash);
            if (trash == nullptr)
                continue;
            std::vector<std::uint32_t> numbers;
            for (const auto &[number, info] : trash->messages())
                numbers.push_back(number);
            for (const std::uint32_t number : numbers)
                if (store->remove(*trash, number))
                    trash->forget(number);
            trash->save();
        }
        publish_tree();
    });
}

void engine::create_folder(const std::string &parent,
                           const std::string &name) {
    submit([this, parent, name] {
        mail::folder_item *item = require(parent);
        if (item->owner().create_folder(*item, name) == nullptr)
            throw std::runtime_error("Could not create folder " + name);
        save_folders();
        publish_tree();
    });
}

void engine::rename_folder(const std::string &id, const std::string &name) {
    submit([this, id, name] {
        mail::folder_item *item = require(id);
        if (item->type != mail::item_type::normal)
            throw std::runtime_error("A special folder cannot be renamed.");
        if (!item->owner().rename_folder(*item, name))
            throw std::runtime_error("Could not rename the folder.");
        save_folders();
        publish_tree();
    });
}

void engine::remove_folder(const std::string &id) {
    submit([this, id] {
        mail::folder_item *item = require(id);
        if (item->type != mail::item_type::normal)
            throw std::runtime_error("A special folder cannot be deleted.");
        if (!item->owner().remove_folder(*item))
            throw std::runtime_error("Could not delete the folder.");
        save_folders();
        publish_tree();
    });
}

void engine::apply_filters(const std::string &id) {
    submit([this, id] {
        mail::folder_item *item = require(id);
        std::vector<std::uint32_t> numbers;
        for (const auto &[number, info] : item->messages())
            numbers.push_back(number);
        const int left =
            filter::apply_rules(rules(), *item, numbers, folders_);
        publish_folder(*item);
        publish_tree();
        const std::string text =
            std::to_string(left) + " messages filtered out";
        jobs_.deliver([this, text] { listener_.status(text); });
    });
}

} // namespace app
