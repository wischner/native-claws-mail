//
// The folder tree and the store-independent operations on it.
//
// Derived from Claws Mail src/folder.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/folder.h"

#include <algorithm>
#include <set>

namespace mail {
namespace {

// Report whether a summary is stale against the store's clock.
bool is_stale(const msg_info &info, std::time_t changed) {
    return changed != 0 && info.mtime != changed;
}

} // namespace

folder_item::folder_item(folder &owner, folder_item *parent,
                         std::string item_name, std::string item_path,
                         item_type kind)
    : name(std::move(item_name)), path(std::move(item_path)), type(kind),
      owner_(&owner), parent_(parent) {}

std::string folder_item::identifier() const {
    std::string id = "#";
    id += owner_->type_name();
    id += "/" + owner_->name();
    if (!path.empty())
        id += "/" + path;
    return id;
}

const msg_map &folder_item::messages() {
    if (!loaded_)
        scan();
    return messages_;
}

void folder_item::scan() {
    if (no_select)
        return;
    if (!loaded_) {
        messages_ = read_msg_cache(owner_->cache_directory(*this));
        loaded_ = true;
    }
    const std::vector<std::uint32_t> present = owner_->list_numbers(*this);
    const std::set<std::uint32_t> wanted(present.begin(), present.end());
    for (auto it = messages_.begin(); it != messages_.end();) {
        if (wanted.contains(it->first)) {
            ++it;
            continue;
        }
        it = messages_.erase(it);
        dirty_ = true;
    }
    for (const std::uint32_t number : present) {
        const auto cached = messages_.find(number);
        const std::time_t changed = owner_->message_time(*this, number);
        if (cached != messages_.end() &&
            !is_stale(cached->second, changed))
            continue;
        auto info = owner_->read_info(*this, number);
        if (!info)
            continue;
        if (cached != messages_.end()) {
            info->perm_flags = cached->second.perm_flags;
        } else if (info->perm_flags == 0) {
            info->perm_flags = flag_new | flag_unread;
        }
        info->mtime = changed;
        messages_[number] = std::move(*info);
        dirty_ = true;
    }
    owner_->scan_finished(*this);
    recount();
    save();
}

const msg_info *folder_item::find(std::uint32_t number) {
    const msg_map &all = messages();
    const auto it = all.find(number);
    return it == all.end() ? nullptr : &it->second;
}

void folder_item::change_flags(std::uint32_t number, std::uint32_t set,
                               std::uint32_t clear) {
    messages();
    const auto it = messages_.find(number);
    if (it == messages_.end())
        return;
    const std::uint32_t before = it->second.perm_flags;
    const std::uint32_t after = (before & ~clear) | set;
    if (before == after)
        return;
    it->second.perm_flags = after;
    dirty_ = true;
    owner_->flags_changed(*this, number, before, after);
    recount();
}

void folder_item::save() {
    if (!dirty_ || !loaded_)
        return;
    if (write_msg_cache(owner_->cache_directory(*this), messages_))
        dirty_ = false;
}

void folder_item::recount() {
    new_count = unread_count = marked_count = 0;
    total_count = static_cast<int>(messages_.size());
    for (const auto &[number, info] : messages_) {
        new_count += info.has(flag_new) ? 1 : 0;
        unread_count += info.has(flag_unread) ? 1 : 0;
        marked_count += info.has(flag_marked) ? 1 : 0;
    }
}

void folder_item::forget(std::uint32_t number) {
    if (messages_.erase(number) != 0) {
        dirty_ = true;
        recount();
    }
}

void folder_item::remember(msg_info info) {
    messages_[info.number] = std::move(info);
    dirty_ = true;
    recount();
}

void folder_item::apply_store_flags(std::uint32_t number,
                                    std::uint32_t perm_flags) {
    const auto it = messages_.find(number);
    if (it == messages_.end() || it->second.perm_flags == perm_flags)
        return;
    it->second.perm_flags = perm_flags;
    dirty_ = true;
}

void folder_item::set_path(std::string value) {
    path = std::move(value);
    for (const auto &child : children)
        child->set_path(path.empty() ? child->name
                                     : path + "/" + child->name);
}

folder::folder(std::string name)
    : name_(std::move(name)),
      root_(std::make_unique<folder_item>(*this, nullptr, name_, "")) {
    root_->no_select = true;
}

folder_item *folder::find_item(std::string_view path) const {
    folder_item *found = nullptr;
    for_each([&](folder_item &item) {
        if (found == nullptr && item.path == path)
            found = &item;
    });
    return found;
}

folder_item *folder::find_special(item_type type) const {
    folder_item *found = nullptr;
    for_each([&](folder_item &item) {
        if (found == nullptr && item.type == type)
            found = &item;
    });
    return found;
}

void folder::for_each(
    const std::function<void(folder_item &)> &visit) const {
    const auto walk = [&](const auto &self, folder_item &item) -> void {
        visit(item);
        for (const auto &child : item.children)
            self(self, *child);
    };
    walk(walk, *root_);
}

std::uint32_t folder::server_copy(folder_item &, folder_item &,
                                  std::uint32_t) {
    return 0;
}

void folder::flags_changed(folder_item &, std::uint32_t, std::uint32_t,
                           std::uint32_t) {}

void folder::scan_finished(folder_item &) {}

std::time_t folder::message_time(folder_item &, std::uint32_t) { return 0; }

void folder::detach_item(folder_item &item) {
    folder_item *parent = item.parent();
    if (parent == nullptr)
        return;
    std::erase_if(parent->children, [&](const auto &child) {
        return child.get() == &item;
    });
}

int transfer_messages(folder_item &from, folder_item &to,
                      const std::vector<std::uint32_t> &numbers,
                      bool keep) {
    if (&from == &to)
        return 0;
    to.messages();
    int moved = 0;
    for (const std::uint32_t number : numbers) {
        const msg_info *info = from.find(number);
        if (info == nullptr)
            continue;
        std::uint32_t added = 0;
        if (&from.owner() == &to.owner())
            added = from.owner().server_copy(from, to, number);
        if (added == 0) {
            const auto raw = from.owner().fetch(from, number);
            if (!raw)
                continue;
            added = to.owner().add(to, *raw, info->perm_flags);
        }
        if (added == 0)
            continue;
        msg_info copied = *info;
        copied.number = added;
        copied.mtime = to.owner().message_time(to, added);
        to.remember(std::move(copied));
        ++moved;
        if (!keep && from.owner().remove(from, number))
            from.forget(number);
    }
    from.save();
    to.save();
    return moved;
}

} // namespace mail
