//
// The folder pane.
//
// Derived from Claws Mail src/folderview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "folder_tree.h"

namespace ui {
namespace {

// Search a branch for an identifier.
const app::folder_node *search(const app::folder_node &node,
                               const std::string &id) {
    if (node.id == id)
        return &node;
    for (const app::folder_node &child : node.children)
        if (const app::folder_node *hit = search(child, id))
            return hit;
    return nullptr;
}

} // namespace

std::string folder_name(const app::folder_node &node) {
    std::string name = node.name;
    if (!node.is_store) {
        switch (node.type) {
        case mail::item_type::inbox:
            name = "Inbox";
            break;
        case mail::item_type::outbox:
            name = "Sent";
            break;
        case mail::item_type::draft:
            name = "Drafts";
            break;
        case mail::item_type::queue:
            name = "Queue";
            break;
        case mail::item_type::trash:
            name = "Trash";
            break;
        case mail::item_type::normal:
            break;
        }
    }
    return name;
}

std::string folder_label(const app::folder_node &node) {
    std::string name = folder_name(node);
    if (node.unread_count > 0)
        name += " (" + std::to_string(node.unread_count) + ")";
    return name;
}

folder_tree::folder_tree() : native::tree_view({}, 0, 0, 220, 400) {
    set_lines_visible(false);
}

native::tree_item_id folder_tree::id_for(const std::string &folder) {
    const auto found = ids_.find(folder);
    if (found != ids_.end())
        return found->second;
    const native::tree_item_id id = next_id_++;
    ids_[folder] = id;
    folders_[id] = folder;
    return id;
}

native::tree_view_item folder_tree::make_item(const app::folder_node &node) {
    const native::tree_item_id id = id_for(node.id);
    native::tree_view_item item = native::tree_node(
        folder_label(node), id, {},
        contains_item(id) ? get_expanded(id) : node.is_store || !node.collapsed,
        !node.no_select || node.is_store);
    for (const app::folder_node &child : node.children)
        item.children.push_back(make_item(child));
    return item;
}

void folder_tree::show_tree(const std::vector<app::folder_node> &tree) {
    const native::tree_item_id selected = get_selected_item();
    std::vector<native::tree_view_item> items;
    for (const app::folder_node &node : tree)
        items.push_back(make_item(node));
    tree_ = tree;
    set_items(std::move(items));
    if (selected != native::invalid_tree_item_id && contains_item(selected))
        set_selected_item(selected);
}

std::string folder_tree::selected_id() const {
    const auto found = folders_.find(get_selected_item());
    return found == folders_.end() ? std::string() : found->second;
}

bool folder_tree::select_id(const std::string &id) {
    const auto found = ids_.find(id);
    if (found == ids_.end() || !contains_item(found->second))
        return false;
    set_selected_item(found->second);
    reveal_item(found->second);
    return true;
}

const app::folder_node *folder_tree::find_node(const std::string &id) const {
    for (const app::folder_node &node : tree_)
        if (const app::folder_node *hit = search(node, id))
            return hit;
    return nullptr;
}

} // namespace ui
