//
// The application window. Its shell is the suite's: the same size, the
// same File, Edit, View first and Help last, the same strips of tools
// and hosted controls and the same four-part status bar as Writer and
// Isaac, around Claws' three panes.
//
// Derived from Claws Mail src/mainwindow.c, src/toolbar.c,
// src/messageview.c and src/gtk/about.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <native.h>

#include "account_window.h"
#include "addr/book.h"
#include "app/engine.h"
#include "app/options.h"
#include "compose_window.h"
#include "folder_tree.h"
#include "form_window.h"
#include "message_pane.h"
#include "settings_windows.h"
#include "summary_table.h"
#include "view/summary.h"

class desktop_test;

namespace ui {

// The main window.
class main_window final : public native::app_wnd,
                          private app::engine_listener {
public:
    // Build the window for the given command line.
    explicit main_window(const app::options &settings);
    ~main_window() override;

    void request_close() override;

private:
    // Drives the window through Native's public input entry points.
    friend class ::desktop_test;

    void build_menu();
    void build_toolbar();
    void build_search_bar();
    void place_children();
    void connect_panes();
    void run(std::string_view command);
    bool run_folder_command(std::string_view command);
    bool run_message_command(std::string_view command);
    bool run_tool_command(std::string_view command);
    void update_state();
    void update_tools();
    void set_message(const std::string &text);

    // Engine results.
    void folders_changed(const std::vector<app::folder_node> &tree) override;
    void folder_loaded(const app::folder_view &view) override;
    void status(const std::string &text) override;
    void failure(const std::string &text) override;
    void mail_received(int count) override;

    void open_selected_folder();
    void show_selection();
    void show_current_message(std::uint32_t number);
    void apply_quick_search();
    void apply_view_filter();
    void set_sort(mail::sort_key key);
    void sort_by_column(native::table_sort sort);
    void go_unread(bool forward);
    void mark_selected(std::uint32_t set, std::uint32_t clear);
    void toggle_mark();
    void delete_selected();
    void transfer_selected(bool keep);
    void reply(compose::reply_mode mode);
    void forward_selected();
    void edit_selected();
    void open_compose(compose::draft text);
    void compose_closed(compose_window *window);
    void new_folder();
    void rename_folder();
    void delete_folder();
    void save_attachment(std::size_t index);
    void find_in_message();
    void import_mailbox();
    void export_mailbox();
    void show_about();
    void start_timer();
    void save_addresses();
    std::vector<std::pair<std::string, std::string>> folder_choices() const;

    app::options settings_;
    view::summary_model model_;
    std::string folder_id_;
    mail::item_type folder_type_ = mail::item_type::normal;
    std::uint32_t shown_number_ = 0;
    bool full_headers_ = false;
    bool updating_ = false;
    std::optional<std::uint32_t> pending_select_;
    std::string message_;
    std::string find_text_;
    addr::book_list books_;

    native::text_edit search_box_;
    native::combo_box show_box_;
    native::combo_box sort_box_;
    folder_tree folders_;
    summary_table summary_;
    message_pane message_pane_;
    native::split_view right_;
    native::split_view split_;
    native::toolbar toolbar_;
    native::toolbar search_bar_;
    native::status_bar status_;
    native::open_file_dialog open_dialog_;
    native::save_file_dialog save_dialog_;
    input_window input_;
    choice_window choice_;
    account_window account_window_;
    preferences_window preferences_window_;
    filter_window filter_window_;
    address_window address_window_;
    log_window log_window_;

    // Command name of each menu id; ids are indices plus one.
    std::vector<std::string> menu_commands_;
    std::vector<std::unique_ptr<compose_window>> composers_;
    std::function<void(const std::filesystem::path &)> open_action_;
    std::function<void(const std::filesystem::path &)> save_action_;

    app::worker_executor jobs_;
    app::engine core_;
    std::jthread timer_;
};

} // namespace ui

#endif
