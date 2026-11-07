//
// The folder pane. folderview.c's GtkCMCTree becomes native::tree_view
// filled from the engine's snapshots, with a map between stable tree
// ids and folder identifiers.
//
// Derived from Claws Mail src/folderview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_FOLDER_TREE_H
#define UI_FOLDER_TREE_H

#include <map>
#include <string>
#include <vector>

#include <native.h>

#include "app/engine.h"

namespace ui {

// Shows the stores and their folders.
class folder_tree final : public native::tree_view {
public:
    // Create an empty pane.
    folder_tree();

    // Show a new snapshot, keeping expansion and selection.
    void show_tree(const std::vector<app::folder_node> &tree);

    // Return the selected folder's identifier, or empty.
    std::string selected_id() const;

    // Select a folder silently; returns whether it exists.
    bool select_id(const std::string &id);

    // Return a node by identifier, or null.
    const app::folder_node *find_node(const std::string &id) const;

    // Return the snapshot.
    const std::vector<app::folder_node> &snapshot() const { return tree_; }

private:
    native::tree_view_item make_item(const app::folder_node &node);
    native::tree_item_id id_for(const std::string &folder);

    std::vector<app::folder_node> tree_;
    std::map<std::string, native::tree_item_id> ids_;
    std::map<native::tree_item_id, std::string> folders_;
    native::tree_item_id next_id_ = 1;
};

// Return a folder's display name.
std::string folder_name(const app::folder_node &node);

// Return the name with its unread count, as the tree shows it.
std::string folder_label(const app::folder_node &node);

} // namespace ui

#endif
