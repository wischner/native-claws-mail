//
// All open stores, and folderlist.xml, which records them. Claws' global
// folder_list and folder_read_list become this object.
//
// Derived from Claws Mail src/folder.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_FOLDER_LIST_H
#define MAIL_FOLDER_LIST_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "mail/folder.h"

namespace mail {

// Creates a store of a type named in folderlist.xml; null means unknown.
class folder_factory {
public:
    virtual ~folder_factory() = default;

    // Create a non-MH store.
    virtual std::unique_ptr<folder>
    create(const std::string &type, const std::string &name,
           int account_id, const std::filesystem::path &path) = 0;
};

// The open stores.
class folder_list {
public:
    //
    // Load folderlist.xml.
    //
    // Parameters:
    //      path        - The file; absent means a first run.
    //      factory     - Makes the store types MH is not, or null.
    //
    // Returns:
    //      Whether the file existed and parsed.
    //
    bool load(const std::filesystem::path &path, folder_factory *factory);

    // Write folderlist.xml; returns whether it succeeded.
    bool save(const std::filesystem::path &path) const;

    // Add a store; returns it.
    folder &add(std::unique_ptr<folder> store);

    // Remove a store.
    void remove(folder &store);

    // Return all stores.
    const std::vector<std::unique_ptr<folder>> &all() const {
        return folders_;
    }

    // Find an item by identifier such as "#mh/Mailbox/inbox", or null.
    folder_item *find(std::string_view identifier) const;

    // Find a special folder for an item's store, then any store, or null.
    folder_item *special_for(const folder_item *item, item_type type) const;

private:
    std::vector<std::unique_ptr<folder>> folders_;
};

} // namespace mail

#endif
