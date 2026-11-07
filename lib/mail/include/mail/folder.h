//
// The mail store. Claws' FolderClass table of function pointers becomes
// the abstract class mail::folder; mail::mh_folder and mail::imap_folder
// implement it, and everything above sees only this interface. What the
// C code did with folder_item_* functions over a GNode is methods on a
// tree of folder_item objects.
//
// A folder is not thread safe. The application touches one from one
// thread at a time; see app::engine.
//
// Derived from Claws Mail src/folder.c and src/folder.h,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_FOLDER_H
#define MAIL_FOLDER_H

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mail/msg_cache.h"

namespace mail {

class folder;

// What a folder is for; names match folderlist.xml.
enum class item_type { normal, inbox, outbox, draft, queue, trash };

// How a folder's messages are listed.
enum class sort_key {
    none, number, size, date, from, subject, score, label,
    mark, unread, mime, to, locked, tags, thread_date
};

// One node of a folder tree.
class folder_item {
public:
    // Create a node.
    folder_item(folder &owner, folder_item *parent, std::string name,
                std::string path, item_type type = item_type::normal);

    std::string name;
    std::string path;           // Relative to the folder root.
    item_type type = item_type::normal;
    bool no_select = false;     // A container; holds no mail.
    bool no_sub = false;
    bool collapsed = false;
    bool threaded = true;
    bool hide_read = false;
    sort_key sort = sort_key::date;
    bool sort_ascending = true;
    int account_id = 0;
    std::time_t mtime = 0;
    std::vector<std::unique_ptr<folder_item>> children;

    // Message counts, as the folder tree shows them.
    int new_count = 0;
    int unread_count = 0;
    int marked_count = 0;
    int total_count = 0;

    // Return the owning store.
    folder &owner() const { return *owner_; }

    // Return the parent node, or null at the root.
    folder_item *parent() const { return parent_; }

    // Return a stable identifier such as "#mh/Mailbox/inbox".
    std::string identifier() const;

    // Return the summaries, scanning the folder on first use.
    const msg_map &messages();

    // Reconcile summaries with the store and refresh the counts.
    void scan();

    // Report whether summaries have been read yet.
    bool loaded() const { return loaded_; }

    // Return one summary, or null.
    const msg_info *find(std::uint32_t number);

    //
    // Change a message's permanent flags.
    //
    // Parameters:
    //      number      - The message.
    //      set         - Bits to set.
    //      clear       - Bits to clear first.
    //
    // Notes:
    //      The store is told so an IMAP server sees the change.
    //
    void change_flags(std::uint32_t number, std::uint32_t set,
                      std::uint32_t clear);

    // Write summaries and flags if anything changed.
    void save();

    // Recount from the summaries.
    void recount();

    // Drop a summary whose message has gone.
    void forget(std::uint32_t number);

    // Add a summary for a new message.
    void remember(msg_info info);

    // Accept flags from the store, without telling the store back.
    void apply_store_flags(std::uint32_t number, std::uint32_t perm_flags);

    // Rebuild the path after a rename; children follow.
    void set_path(std::string value);

private:
    friend class folder;

    folder *owner_;
    folder_item *parent_;
    msg_map messages_;
    bool loaded_ = false;
    bool dirty_ = false;
};

// A mail store: one tree of folders.
class folder {
public:
    // Create a store.
    explicit folder(std::string name);
    virtual ~folder() = default;

    folder(const folder &) = delete;
    folder &operator=(const folder &) = delete;

    // Return "mh" or "imap", as folderlist.xml names the class.
    virtual std::string_view type_name() const = 0;

    // Return the store's display name.
    const std::string &name() const { return name_; }

    // Rename the store.
    void set_name(std::string value) { name_ = std::move(value); }

    // Return the root item, which exists for every store.
    folder_item &root() const { return *root_; }

    // Return the account bound to this store, or 0.
    int account_id() const { return account_id_; }

    // Bind an account.
    void set_account_id(int id) { account_id_ = id; }

    // Find an item by path, or null.
    folder_item *find_item(std::string_view path) const;

    // Find the first item of a type, or null.
    folder_item *find_special(item_type type) const;

    // Call visit on every item, parents first.
    void for_each(const std::function<void(folder_item &)> &visit) const;

    // Return message numbers present in an item.
    virtual std::vector<std::uint32_t> list_numbers(folder_item &item) = 0;

    // Return a summary for a message not yet cached.
    virtual std::optional<msg_info> read_info(folder_item &item,
                                              std::uint32_t number) = 0;

    // Return a message's complete raw text.
    virtual std::optional<std::string> fetch(folder_item &item,
                                             std::uint32_t number) = 0;

    //
    // Store a message.
    //
    // Parameters:
    //      item        - Destination.
    //      raw         - Complete message.
    //      perm_flags  - Flags for the stored message.
    //
    // Returns:
    //      Its new number, or 0 on failure.
    //
    virtual std::uint32_t add(folder_item &item, std::string_view raw,
                              std::uint32_t perm_flags) = 0;

    // Remove a message; returns whether it is gone.
    virtual bool remove(folder_item &item, std::uint32_t number) = 0;

    // Copy messages between items of this store; zero means unsupported.
    virtual std::uint32_t server_copy(folder_item &from, folder_item &to,
                                      std::uint32_t number);

    // Tell the store about flag changes.
    virtual void flags_changed(folder_item &item, std::uint32_t number,
                               std::uint32_t old_flags,
                               std::uint32_t new_flags);

    // Called when a scan has reconciled summaries.
    virtual void scan_finished(folder_item &item);

    // Return when a message last changed, or 0 when that is unknown.
    virtual std::time_t message_time(folder_item &item,
                                     std::uint32_t number);

    // Create a child folder; returns null on failure.
    virtual folder_item *create_folder(folder_item &parent,
                                       const std::string &name) = 0;

    // Rename a folder; returns whether it worked.
    virtual bool rename_folder(folder_item &item,
                               const std::string &name) = 0;

    // Delete a folder and its mail; returns whether it worked.
    virtual bool remove_folder(folder_item &item) = 0;

    // Rediscover the folder tree.
    virtual void scan_tree() = 0;

    // Return where this item keeps its cache files.
    virtual std::filesystem::path cache_directory(
        const folder_item &item) const = 0;

protected:
    // Remove an item from its parent and destroy it.
    void detach_item(folder_item &item);

    int account_id_ = 0;

private:
    std::string name_;
    std::unique_ptr<folder_item> root_;
};

//
// Move or copy messages between any two items.
//
// Parameters:
//      from, to    - Source and destination.
//      numbers     - Messages in the source.
//      keep        - Copy rather than move.
//
// Notes:
//      Within one store a server copy is used when it exists. A failure
//      leaves the source message in place, so mail is never lost.
//
// Returns:
//      How many messages reached the destination.
//
int transfer_messages(folder_item &from, folder_item &to,
                      const std::vector<std::uint32_t> &numbers, bool keep);

} // namespace mail

#endif
