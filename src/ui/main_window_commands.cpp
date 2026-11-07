//
// The menu bar and what each command does. A menu row and a toolbar
// button run the same command, as in Writer and Isaac: File, Edit and
// View come first, Help last, with Message and Tools holding what a
// mail program adds.
//
// Derived from Claws Mail src/mainwindow.c and src/gtk/about.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>

#include "main_window.h"

namespace ui {
namespace {

// One menu row; an empty command is a separator.
struct menu_row {
    const char *label;
    const char *command;
};

// One top-level menu.
struct menu_group {
    const char *title;
    std::vector<menu_row> rows;
};

// The menu bar.
const std::vector<menu_group> &menu_groups() {
    static const std::vector<menu_group> groups = {
        {"&File",
         {{"&New Message\tCtrl+N", "fileNew"},
          {"Open &Draft", "editMessage"},
          {"", ""},
          {"&Import Mailbox...", "importMbox"},
          {"&Export Mailbox...", "exportMbox"},
          {"", ""},
          {"E&xit\tCtrl+Q", "quit"}}},
        {"&Edit",
         {{"&Copy\tCtrl+C", "copy"},
          {"Select &All\tCtrl+A", "selectAll"},
          {"", ""},
          {"&Find in Message...\tCtrl+F", "find"},
          {"Find &Next\tF3", "findNext"},
          {"&Search Folder\tCtrl+Shift+F", "focusSearch"}}},
        {"&View",
         {{"&Next Unread\tCtrl+.", "nextUnread"},
          {"&Previous Unread\tCtrl+,", "previousUnread"},
          {"", ""},
          {"&Threads", "threadView"},
          {"&All Headers", "toggleHeaders"},
          {"", ""},
          {"&Refresh Folder\tF5", "refreshFolder"}}},
        {"&Message",
         {{"&Get Mail\tCtrl+I", "getMail"},
          {"&Send Queue\tCtrl+Shift+S", "sendMail"},
          {"", ""},
          {"&Reply\tCtrl+R", "reply"},
          {"Reply to &All\tCtrl+Shift+R", "replyAll"},
          {"Reply to &List", "replyList"},
          {"&Forward\tCtrl+L", "forward"},
          {"", ""},
          {"&Move to Folder...\tCtrl+M", "moveMessage"},
          {"Cop&y to Folder...", "copyMessage"},
          {"&Delete\tCtrl+D", "deleteMail"},
          {"", ""},
          {"Mar&k\tCtrl+Shift+K", "markMessage"},
          {"Mark &Read", "markRead"},
          {"Mark &Unread", "markUnread"},
          {"L&ock", "lockMessage"},
          {"Unl&ock", "unlockMessage"}}},
        {"&Tools",
         {{"&Accounts...", "accounts"},
          {"&Preferences...", "preferences"},
          {"&Filter Rules...", "filters"},
          {"Address &Book\tCtrl+Shift+A", "addressBook"},
          {"", ""},
          {"Apply Filters to Folder", "applyFilters"},
          {"&New Folder...", "newFolder"},
          {"Re&name Folder...", "renameFolder"},
          {"&Delete Folder", "deleteFolder"},
          {"&Empty Trash", "emptyTrash"},
          {"", ""},
          {"Network &Log", "networkLog"}}},
        {"&Help", {{"&About Blaise", "about"}}},
    };
    return groups;
}

} // namespace

void main_window::build_menu() {
    for (const menu_group &group : menu_groups()) {
        native::menu_items_proxy items;
        for (const menu_row &row : group.rows) {
            if (row.command[0] == '\0') {
                items << native::menu_separator;
                continue;
            }
            menu_commands_.emplace_back(row.command);
            items << std::make_pair(static_cast<int>(menu_commands_.size()),
                                    std::string(row.label));
        }
        menu << group.title << items;
    }
}

bool main_window::run_folder_command(std::string_view command) {
    if (command == "newFolder")
        new_folder();
    else if (command == "renameFolder")
        rename_folder();
    else if (command == "deleteFolder")
        delete_folder();
    else if (command == "emptyTrash")
        core_.empty_trash();
    else if (command == "applyFilters" && !folder_id_.empty())
        core_.apply_filters(folder_id_);
    else if (command == "refreshFolder" && !folder_id_.empty())
        core_.open_folder(folder_id_);
    else if (command == "importMbox")
        import_mailbox();
    else if (command == "exportMbox")
        export_mailbox();
    else
        return false;
    return true;
}

bool main_window::run_message_command(std::string_view command) {
    if (command == "reply")
        reply(compose::reply_mode::sender);
    else if (command == "replyAll")
        reply(compose::reply_mode::all);
    else if (command == "replyList")
        reply(compose::reply_mode::list);
    else if (command == "forward")
        forward_selected();
    else if (command == "editMessage")
        edit_selected();
    else if (command == "deleteMail")
        delete_selected();
    else if (command == "moveMessage")
        transfer_selected(false);
    else if (command == "copyMessage")
        transfer_selected(true);
    else if (command == "markMessage")
        toggle_mark();
    else if (command == "markRead")
        mark_selected(0, mail::flag_new | mail::flag_unread);
    else if (command == "markUnread")
        mark_selected(mail::flag_unread, 0);
    else if (command == "lockMessage")
        mark_selected(mail::flag_locked, 0);
    else if (command == "unlockMessage")
        mark_selected(0, mail::flag_locked);
    else if (command == "nextUnread")
        go_unread(true);
    else if (command == "previousUnread")
        go_unread(false);
    else
        return false;
    return true;
}

bool main_window::run_tool_command(std::string_view command) {
    if (command == "accounts")
        account_window_.edit(core_.accounts());
    else if (command == "preferences")
        preferences_window_.edit(core_.preferences());
    else if (command == "filters")
        filter_window_.edit(core_.rules());
    else if (command == "addressBook")
        address_window_.open();
    else if (command == "networkLog")
        log_window_.open();
    else if (command == "about")
        show_about();
    else
        return false;
    return true;
}

void main_window::run(std::string_view command) {
    if (run_folder_command(command) || run_message_command(command) ||
        run_tool_command(command))
        return;
    if (command == "fileNew") {
        const prefs::account_list accounts = core_.accounts();
        const prefs::account *item = accounts.default_account();
        if (item == nullptr) {
            set_message("Add an account under Tools, Accounts first.");
            return;
        }
        open_compose(compose::new_draft(*item));
    } else if (command == "getMail") {
        set_message("Getting mail...");
        core_.receive_all();
    } else if (command == "sendMail") {
        core_.send_queue();
    } else if (command == "copy") {
        message_pane_.copy();
    } else if (command == "selectAll") {
        message_pane_.select_all();
    } else if (command == "find") {
        find_in_message();
    } else if (command == "findNext") {
        if (!message_pane_.find_next(find_text_))
            set_message("Not found");
    } else if (command == "focusSearch") {
        search_box_.set_text("");
    } else if (command == "threadView") {
        model_.set_threaded(!model_.threaded());
        summary_.refresh();
        if (!folder_id_.empty())
            core_.set_folder_view(folder_id_, model_.sort(),
                                  model_.sort_ascending(), model_.threaded());
        update_state();
    } else if (command == "toggleHeaders") {
        full_headers_ = !full_headers_;
        if (!message_pane_.raw().empty())
            message_pane_.show_message(message_pane_.raw(), full_headers_);
    } else if (command == "quit") {
        request_close();
    }
}

void main_window::find_in_message() {
    input_.ask("Find in Message", "Find:", find_text_,
               [this](const std::string &text) {
                   find_text_ = text;
                   if (!message_pane_.find_next(text))
                       set_message("Not found");
               });
}

void main_window::new_folder() {
    std::string parent = folders_.selected_id();
    if (parent.empty())
        return;
    input_.ask("New Folder", "Folder name:", "",
               [this, parent](const std::string &name) {
                   core_.create_folder(parent, name);
               });
}

void main_window::rename_folder() {
    const std::string id = folders_.selected_id();
    const app::folder_node *node = folders_.find_node(id);
    if (node == nullptr || node->is_store ||
        node->type != mail::item_type::normal) {
        set_message("That folder cannot be renamed.");
        return;
    }
    input_.ask("Rename Folder", "New name:", node->name,
               [this, id](const std::string &name) {
                   core_.rename_folder(id, name);
                   if (id == folder_id_)
                       folder_id_.clear();
               });
}

void main_window::delete_folder() {
    const std::string id = folders_.selected_id();
    const app::folder_node *node = folders_.find_node(id);
    if (node == nullptr || node->is_store ||
        node->type != mail::item_type::normal) {
        set_message("That folder cannot be deleted.");
        return;
    }
    if (native::message_box::show(
            *this, "Delete " + node->name + " and all the mail in it?",
            "Blaise", native::message_box_buttons::yes_no,
            native::message_box_icon::warning) !=
        native::message_box_result::yes)
        return;
    if (id == folder_id_) {
        folder_id_.clear();
        model_.set_messages({});
        summary_.refresh();
        message_pane_.clear();
    }
    core_.remove_folder(id);
}

void main_window::import_mailbox() {
    if (folder_id_.empty())
        return;
    open_action_ = [this](const std::filesystem::path &path) {
        core_.import_mbox(path, folder_id_);
    };
    open_dialog_.show();
}

void main_window::export_mailbox() {
    if (folder_id_.empty())
        return;
    save_action_ = [this](const std::filesystem::path &path) {
        core_.export_mbox(folder_id_, path);
    };
    save_dialog_.show();
}

void main_window::show_about() {
    native::message_box::show(
        *this,
        "Blaise " BLAISE_VERSION "\n\nA mail client rebuilt from Claws Mail "
        "on the Native UI library.\n\nGNU General Public License, version 3 "
        "or later.",
        "About Blaise", native::message_box_buttons::ok,
        native::message_box_icon::information);
}

} // namespace ui
