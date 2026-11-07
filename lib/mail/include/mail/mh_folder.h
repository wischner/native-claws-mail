//
// The MH store: one directory per folder, one numbered file per message.
// This is Claws' own local format, and Blaise opens an existing ~/Mail
// in place.
//
// Derived from Claws Mail src/mh.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_MH_FOLDER_H
#define MAIL_MH_FOLDER_H

#include <filesystem>

#include "mail/folder.h"

namespace mail {

// A folder tree on disk.
class mh_folder final : public folder {
public:
    //
    // Open a store.
    //
    // Parameters:
    //      name        - Display name, conventionally "Mailbox".
    //      directory   - Root directory; created when absent, along with
    //                    the standard inbox, sent, queue, draft and trash.
    //
    mh_folder(std::string name, std::filesystem::path directory);

    std::string_view type_name() const override { return "mh"; }

    // Return the root directory.
    const std::filesystem::path &directory() const { return directory_; }

    std::vector<std::uint32_t> list_numbers(folder_item &item) override;
    std::optional<msg_info> read_info(folder_item &item,
                                      std::uint32_t number) override;
    std::optional<std::string> fetch(folder_item &item,
                                     std::uint32_t number) override;
    std::uint32_t add(folder_item &item, std::string_view raw,
                      std::uint32_t perm_flags) override;
    bool remove(folder_item &item, std::uint32_t number) override;
    std::time_t message_time(folder_item &item,
                             std::uint32_t number) override;
    folder_item *create_folder(folder_item &parent,
                               const std::string &name) override;
    bool rename_folder(folder_item &item, const std::string &name) override;
    bool remove_folder(folder_item &item) override;
    void scan_tree() override;
    std::filesystem::path cache_directory(
        const folder_item &item) const override;

private:
    std::filesystem::path item_path(const folder_item &item) const;
    void scan_directory(folder_item &parent);

    std::filesystem::path directory_;
};

} // namespace mail

#endif
