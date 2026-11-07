//
// The application core. Claws spreads this across mainwindow.c, inc.c,
// procmsg.c and folder.c, all reaching for globals; here one object owns
// the stores, accounts and rules, every method is a job on the executor,
// and results come back as snapshots through engine_listener. The window
// holds no store pointer, which is what makes a blocking IMAP call safe
// while it stays responsive.
//
// Derived from Claws Mail src/main.c, src/inc.c, src/procmsg.c and
// src/send_message.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef APP_ENGINE_H
#define APP_ENGINE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/executor.h"
#include "compose/draft.h"
#include "filter/rules.h"
#include "mail/folder_list.h"
#include "net/transport.h"
#include "prefs/account.h"
#include "prefs/common.h"

namespace app {

// Where Blaise keeps things.
struct paths {
    std::filesystem::path config;
    std::filesystem::path mail;

    // Return the defaults: ~/.blaise and ~/Mail.
    static paths defaults();
};

// One folder as the tree shows it.
struct folder_node {
    std::string id;
    std::string name;
    mail::item_type type = mail::item_type::normal;
    bool is_store = false;
    bool no_select = false;
    bool collapsed = false;
    int new_count = 0;
    int unread_count = 0;
    int total_count = 0;
    std::vector<folder_node> children;
};

// A folder's list, ready to show.
struct folder_view {
    std::string id;
    std::string name;
    mail::item_type type = mail::item_type::normal;
    mail::sort_key sort = mail::sort_key::date;
    bool sort_ascending = true;
    bool threaded = true;
    std::vector<mail::msg_info> messages;
};

// How the engine talks back; every call is on the interface thread.
class engine_listener {
public:
    virtual ~engine_listener() = default;

    // The folder tree or its counts changed.
    virtual void folders_changed(const std::vector<folder_node> &tree) = 0;

    // A folder's contents arrived or changed.
    virtual void folder_loaded(const folder_view &view) = 0;

    // Something worth a line in the status bar happened.
    virtual void status(const std::string &text) = 0;

    // Something failed that the user must know about.
    virtual void failure(const std::string &text) = 0;

    // Mail arrived; count is the number of new messages.
    virtual void mail_received(int count) = 0;
};

// The application core.
class engine {
public:
    //
    // Create the engine; nothing runs until load().
    //
    // Parameters:
    //      where       - Configuration and mail directories.
    //      jobs        - Runs the work.
    //      listener    - Receives results; must outlive the engine.
    //      connect     - Opens connections.
    //
    engine(paths where, executor &jobs, engine_listener &listener,
           net::connector connect = net::socket_connector());

    // Open configuration and stores.
    void load();

    // Flush everything; call before destruction, on the worker's queue.
    void shut_down();

    // Return a copy of the accounts. Safe from any thread.
    prefs::account_list accounts() const;

    // Replace the accounts and save them.
    void set_accounts(prefs::account_list list);

    // Return a copy of the preferences. Safe from any thread.
    prefs::common preferences() const;

    // Replace the preferences and save them.
    void set_preferences(prefs::common values);

    // Return a copy of the filter rules. Safe from any thread.
    std::vector<filter::rule> rules() const;

    // Replace the filter rules and save them.
    void set_rules(std::vector<filter::rule> rules);

    // Rescan the folder tree of every store.
    void refresh_tree();

    // Load a folder.
    void open_folder(const std::string &id);

    // Remember a folder's display choices.
    void set_folder_view(const std::string &id, mail::sort_key sort,
                         bool ascending, bool threaded);

    // Fetch a message; the callback runs on the interface thread.
    void fetch_message(
        const std::string &id, std::uint32_t number,
        std::function<void(std::optional<std::string>)> done);

    //
    // Search a folder, reading message text where the conditions need it.
    //
    // Parameters:
    //      id          - The folder.
    //      conditions  - What to look for.
    //      done        - Receives the matching numbers on the interface
    //                    thread.
    //
    void search(const std::string &id, filter::condition_list conditions,
                std::function<void(std::set<std::uint32_t>)> done);

    // Change flags on messages.
    void change_flags(const std::string &id,
                      std::vector<std::uint32_t> numbers, std::uint32_t set,
                      std::uint32_t clear);

    // Move or copy messages.
    void transfer(const std::string &from, std::vector<std::uint32_t> numbers,
                  const std::string &to, bool keep);

    // Delete messages: to the trash, or for good from the trash.
    void delete_messages(const std::string &id,
                         std::vector<std::uint32_t> numbers);

    // Empty every trash folder.
    void empty_trash();

    // Make a folder under a parent.
    void create_folder(const std::string &parent, const std::string &name);

    // Rename a folder.
    void rename_folder(const std::string &id, const std::string &name);

    // Delete a folder and its mail.
    void remove_folder(const std::string &id);

    // Run the filter rules over a folder.
    void apply_filters(const std::string &id);

    // Collect mail for every account set to be collected.
    void receive_all();

    // Collect mail for one account.
    void receive_account(int account_id);

    //
    // Send a message.
    //
    // Notes:
    //      The message goes through the queue, so a failed send keeps it
    //      safe in the queue folder to try again.
    //
    void send(compose::draft text);

    // Queue a message without sending it.
    void queue(compose::draft text);

    // Save a message to the drafts folder.
    void save_draft(compose::draft text);

    // Send everything waiting in the queue.
    void send_queue();

    // Copy an mbox file into a folder.
    void import_mbox(const std::filesystem::path &file,
                     const std::string &id);

    // Write a folder to an mbox file.
    void export_mbox(const std::string &id,
                     const std::filesystem::path &file);

    // Copy Claws Mail's configuration, once, into an empty directory.
    static bool import_claws(const std::filesystem::path &claws_directory,
                             const std::filesystem::path &config);

private:
    using job = std::function<void()>;

    void submit(job work);
    void publish_tree();
    void publish_folder(mail::folder_item &item);
    void report(const std::string &text);
    void sync_stores();
    std::unique_ptr<mail::folder> make_imap(const std::string &name,
                                            const prefs::account &item,
                                            int account_id);
    mail::folder_item *require(const std::string &id);
    mail::folder_item *target_for(const prefs::account &item,
                                  const std::string &chosen,
                                  mail::item_type type);
    const prefs::account *find_account(int id) const;
    void receive_one(const prefs::account &item, int &total);
    int receive_pop3(const prefs::account &item);
    int deliver_new(mail::folder_item &inbox, const prefs::account &item,
                    const std::vector<std::uint32_t> &numbers);
    void queue_sub(const compose::draft &text);
    void remove_source(const std::string &source);
    void send_one(mail::folder &store, mail::folder_item &queue,
                  std::uint32_t number, const prefs::account_list &list);
    void send_queue_sub();
    void mark_source(const std::string &source, std::uint32_t flag);
    void save_folders();
    std::filesystem::path uid_file(const prefs::account &item) const;

    class imap_factory;

    paths paths_;
    executor &jobs_;
    engine_listener &listener_;
    net::connector connect_;
    mail::folder_list folders_;
    mutable std::mutex settings_lock_;
    prefs::account_list accounts_;
    prefs::common preferences_;
    filter::rule_set rules_;
};

} // namespace app

#endif
