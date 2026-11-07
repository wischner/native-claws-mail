//
// What the user does to messages: opening folders, reading, searching,
// sorting, flagging, moving and starting replies. Results come back
// through the engine, and the window updates from them.
//
// Derived from Claws Mail src/mainwindow.c and src/summaryview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>

#include "filter/matcher.h"
#include "main_window.h"
#include "util/files.h"

namespace ui {

void main_window::open_selected_folder() {
    const std::string id = folders_.selected_id();
    if (id.empty() || id == folder_id_)
        return;
    const app::folder_node *node = folders_.find_node(id);
    if (node == nullptr || node->no_select)
        return;
    folder_id_ = id;
    shown_number_ = 0;
    message_pane_.clear();
    model_.set_matches(std::nullopt);
    model_.set_messages({});
    summary_.refresh();
    core_.open_folder(id);
    update_state();
}

void main_window::show_selection() {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    update_state();
    if (numbers.size() != 1) {
        return;
    }
    show_current_message(numbers.front());
}

void main_window::show_current_message(std::uint32_t number) {
    if (number == shown_number_)
        return;
    shown_number_ = number;
    const std::string id = folder_id_;
    core_.fetch_message(id, number,
                        [this, id, number](std::optional<std::string> raw) {
                            if (id != folder_id_ || number != shown_number_)
                                return;
                            if (raw)
                                message_pane_.show_message(std::move(*raw),
                                                           full_headers_);
                            else
                                message_pane_.show_note(
                                    "The message could not be read.\n");
                        });
    const auto row = model_.row_of(number);
    if (row && model_.at(*row).has(mail::flag_unread) &&
        core_.preferences().mark_read_on_open) {
        const std::uint32_t kept = model_.at(*row).perm_flags &
                                   ~(mail::flag_new | mail::flag_unread);
        model_.set_flags(number, kept);
        core_.change_flags(id, {number}, 0,
                           mail::flag_new | mail::flag_unread);
        summary_.refresh();
    }
}

void main_window::apply_quick_search() {
    const std::string text = search_box_.get_text();
    auto conditions = filter::parse_quick_search(text);
    if (conditions && conditions->needs_content() && !folder_id_.empty()) {
        // Reading mail can take a while, and over IMAP it blocks: the
        // engine does it, and a later keystroke makes this answer stale.
        model_.set_filter(std::nullopt);
        core_.search(folder_id_, std::move(*conditions),
                     [this, text](std::set<std::uint32_t> found) {
                         if (text != search_box_.get_text())
                             return;
                         model_.set_matches(std::move(found));
                         summary_.refresh();
                         update_state();
                     });
        return;
    }
    model_.set_matches(std::nullopt);
    model_.set_filter(std::move(conditions));
    summary_.refresh();
    update_state();
}

void main_window::apply_view_filter() {
    model_.set_hide_read(show_box_.get_selected_index() == 1);
    summary_.refresh();
    update_state();
}

void main_window::set_sort(mail::sort_key key) {
    model_.set_sort(key, model_.sort() == key ? !model_.sort_ascending()
                                              : model_.sort_ascending());
    summary_.refresh();
    if (!folder_id_.empty())
        core_.set_folder_view(folder_id_, model_.sort(),
                              model_.sort_ascending(), model_.threaded());
    update_state();
}

void main_window::sort_by_column(native::table_sort sort) {
    mail::sort_key key = mail::sort_key::date;
    switch (sort.column) {
    case summary_source::status:
        key = mail::sort_key::unread;
        break;
    case summary_source::subject:
        key = mail::sort_key::subject;
        break;
    case summary_source::from:
        key = mail::sort_key::from;
        break;
    case summary_source::size:
        key = mail::sort_key::size;
        break;
    default:
        break;
    }
    set_sort(key);
    summary_.set_sort(native::table_sort{
        sort.column, model_.sort_ascending()
                         ? native::sort_direction::ascending
                         : native::sort_direction::descending});
}

void main_window::go_unread(bool forward) {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    std::optional<std::size_t> row;
    const auto current = numbers.empty()
                             ? std::nullopt
                             : model_.row_of(numbers.back());
    if (!current)
        row = forward ? model_.first_unread() : std::nullopt;
    else
        row = forward ? model_.next_unread(*current)
                      : model_.previous_unread(*current);
    if (!row) {
        set_message("No more unread messages");
        return;
    }
    summary_.select_number(model_.at(*row).number);
    show_selection();
}

void main_window::mark_selected(std::uint32_t set, std::uint32_t clear) {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    if (numbers.empty())
        return;
    for (const std::uint32_t number : numbers)
        if (const auto row = model_.row_of(number))
            model_.set_flags(number,
                             (model_.at(*row).perm_flags & ~clear) | set);
    core_.change_flags(folder_id_, numbers, set, clear);
    summary_.refresh();
    update_state();
}

void main_window::toggle_mark() {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    if (numbers.empty())
        return;
    const auto first = model_.row_of(numbers.front());
    if (first && model_.at(*first).has(mail::flag_marked))
        mark_selected(0, mail::flag_marked);
    else
        mark_selected(mail::flag_marked, 0);
}

void main_window::delete_selected() {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    if (numbers.empty())
        return;
    if (core_.preferences().ask_on_delete &&
        native::message_box::show(*this, "Delete the selected messages?",
                                  "Blaise",
                                  native::message_box_buttons::yes_no,
                                  native::message_box_icon::question) !=
            native::message_box_result::yes)
        return;
    // Move on to the next row, as a reader expects.
    const auto last = model_.row_of(numbers.back());
    if (last && *last + 1 < model_.rows().size())
        pending_select_ = model_.at(*last + 1).number;
    shown_number_ = 0;
    core_.delete_messages(folder_id_, numbers);
}

std::vector<std::pair<std::string, std::string>>
main_window::folder_choices() const {
    std::vector<std::pair<std::string, std::string>> choices;
    const auto walk = [&](const auto &self, const app::folder_node &node,
                          const std::string &prefix) -> void {
        const std::string label = prefix + folder_name(node);
        if (!node.is_store && !node.no_select)
            choices.emplace_back(label, node.id);
        for (const app::folder_node &child : node.children)
            self(self, child, label + "/");
    };
    for (const app::folder_node &store : folders_.snapshot())
        walk(walk, store, "");
    return choices;
}

void main_window::transfer_selected(bool keep) {
    const std::vector<std::uint32_t> numbers = summary_.selected_numbers();
    if (numbers.empty())
        return;
    auto choices = folder_choices();
    std::vector<std::string> labels;
    for (const auto &[label, id] : choices)
        labels.push_back(label);
    choice_.ask(keep ? "Copy to Folder" : "Move to Folder", std::move(labels),
                [this, numbers, keep, choices](std::size_t index) {
                    shown_number_ = 0;
                    core_.transfer(folder_id_, numbers, choices[index].second,
                                   keep);
                });
}

void main_window::open_compose(compose::draft text) {
    auto window = std::make_unique<compose_window>(
        *this, core_, books_, std::move(text),
        [this](compose_window *closed) { compose_closed(closed); });
    window->create();
    window->center_to_parent();
    window->show();
    composers_.push_back(std::move(window));
}

void main_window::compose_closed(compose_window *window) {
    window->destroy();
    std::erase_if(composers_,
                  [&](const auto &item) { return item.get() == window; });
}

void main_window::reply(compose::reply_mode mode) {
    if (message_pane_.raw().empty())
        return;
    const std::string raw = message_pane_.raw();
    const prefs::account_list accounts = core_.accounts();
    const prefs::account *item = compose::account_for_reply(raw, accounts);
    if (item == nullptr) {
        set_message("Add an account under Tools, Accounts first.");
        return;
    }
    compose::draft text =
        compose::reply_to(raw, *item, core_.preferences(), mode);
    text.reply_source = folder_id_ + "\t" + std::to_string(shown_number_);
    open_compose(std::move(text));
}

void main_window::forward_selected() {
    if (message_pane_.raw().empty())
        return;
    const prefs::account_list accounts = core_.accounts();
    const prefs::account *item =
        compose::account_for_reply(message_pane_.raw(), accounts);
    if (item == nullptr) {
        set_message("Add an account under Tools, Accounts first.");
        return;
    }
    compose::draft text =
        compose::forward(message_pane_.raw(), *item, core_.preferences());
    text.forward_source = folder_id_ + "\t" + std::to_string(shown_number_);
    open_compose(std::move(text));
}

void main_window::edit_selected() {
    if (message_pane_.raw().empty())
        return;
    compose::draft text = compose::reopen(message_pane_.raw());
    if (folder_type_ == mail::item_type::draft ||
        folder_type_ == mail::item_type::queue)
        text.replaces = folder_id_ + "\t" + std::to_string(shown_number_);
    open_compose(std::move(text));
}

void main_window::save_attachment(std::size_t index) {
    const auto &files = message_pane_.document().attachments;
    if (index >= files.size())
        return;
    const std::string data = view::attachment_data(message_pane_.raw(), index);
    save_action_ = [this, data](const std::filesystem::path &path) {
        set_message(util::write_file(path, data) ? "Saved " + path.string()
                                                 : "Could not save the file");
    };
    save_dialog_.set_initial_path(files[index].name);
    save_dialog_.show();
}

} // namespace ui
