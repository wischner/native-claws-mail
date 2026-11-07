//
// An IMAP account as a folder tree. Messages keep their server UID as
// their number, read mail is kept in a local cache directory, and flags
// flow both ways. Every method blocks on the network, which is why the
// engine runs them on its worker thread.
//
// Derived from Claws Mail src/imap.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_IMAP_FOLDER_H
#define MAIL_IMAP_FOLDER_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "mail/folder.h"
#include "net/imap_client.h"

namespace mail {

// How to reach an IMAP account.
struct imap_settings {
    net::endpoint server;
    std::string user;
    std::string password;
    std::string directory;      // Reference prefix, usually empty.
    bool subscribed_only = false;
    net::connector connect;
};

// An IMAP store.
class imap_folder final : public folder {
public:
    //
    // Open a store; nothing connects until first needed.
    //
    // Parameters:
    //      name        - Display name.
    //      settings    - The account.
    //      cache       - Directory for cached mail and summaries.
    //
    imap_folder(std::string name, imap_settings settings,
                std::filesystem::path cache);

    std::string_view type_name() const override { return "imap"; }

    // Replace credentials and server details after the account changes.
    void update_settings(imap_settings settings);

    std::vector<std::uint32_t> list_numbers(folder_item &item) override;
    std::optional<msg_info> read_info(folder_item &item,
                                      std::uint32_t number) override;
    std::optional<std::string> fetch(folder_item &item,
                                     std::uint32_t number) override;
    std::uint32_t add(folder_item &item, std::string_view raw,
                      std::uint32_t perm_flags) override;
    bool remove(folder_item &item, std::uint32_t number) override;
    std::uint32_t server_copy(folder_item &from, folder_item &to,
                              std::uint32_t number) override;
    void flags_changed(folder_item &item, std::uint32_t number,
                       std::uint32_t old_flags,
                       std::uint32_t new_flags) override;
    void scan_finished(folder_item &item) override;
    folder_item *create_folder(folder_item &parent,
                               const std::string &name) override;
    bool rename_folder(folder_item &item, const std::string &name) override;
    bool remove_folder(folder_item &item) override;
    void scan_tree() override;
    std::filesystem::path cache_directory(
        const folder_item &item) const override;

private:
    net::imap_client &session();
    void select(folder_item &item);
    void drop_session();
    std::string mailbox_name(const folder_item &item) const;
    std::filesystem::path message_path(const folder_item &item,
                                       std::uint32_t number) const;
    template <typename Call>
    auto with_retry(Call &&call);

    imap_settings settings_;
    std::filesystem::path cache_;
    std::unique_ptr<net::imap_client> client_;
    const folder_item *selected_ = nullptr;
    std::map<std::string, std::string> raw_names_;  // By item path.
    std::map<std::uint32_t, net::imap_message> pending_;
    std::map<std::uint32_t, std::uint32_t> remote_flags_;
    char delimiter_ = '/';
};

// Return permanent flags for a server's flag set.
std::uint32_t flags_from_imap(const std::set<std::string> &flags,
                              std::uint32_t local);

} // namespace mail

#endif
