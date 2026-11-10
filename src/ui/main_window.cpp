//
// Window construction and state display. After every change the
// controls are re-read from the models, so tools and status bar cannot
// disagree with what the panes show.
//
// Derived from Claws Mail src/mainwindow.c and src/summaryview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "main_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>

#include "filter/matcher.h"
#include "mime/encoded_word.h"
#include "tool_icons.h"
#include "util/files.h"
#include "util/text.h"

namespace ui {
namespace {

constexpr int search_length = 320;

// One toolbar entry; an empty command is a separator.
struct tool_entry {
    const char *command;
    native::tool_kind kind;
};

constexpr std::array<tool_entry, 17> tools{{
    {"fileNew", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"getMail", native::tool_kind::button},
    {"sendMail", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"reply", native::tool_kind::button},
    {"replyAll", native::tool_kind::button},
    {"forward", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"copy", native::tool_kind::button},
    {"find", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"deleteMail", native::tool_kind::button},
    {"markMessage", native::tool_kind::button},
    {"previousUnread", native::tool_kind::button},
    {"nextUnread", native::tool_kind::button},
    {"addressBook", native::tool_kind::button},
}};

// One sort choice of the search bar.
struct sort_choice {
    const char *label;
    mail::sort_key key;
};

constexpr std::array<sort_choice, 6> sort_choices{{
    {"Date", mail::sort_key::date},
    {"From", mail::sort_key::from},
    {"Subject", mail::sort_key::subject},
    {"Size", mail::sort_key::size},
    {"Unread", mail::sort_key::unread},
    {"Number", mail::sort_key::number},
}};

// Return the labels of the sort box.
std::vector<std::string> sort_labels() {
    std::vector<std::string> labels;
    for (const sort_choice &choice : sort_choices)
        labels.emplace_back(choice.label);
    return labels;
}

// Return a tool item for one table entry.
native::tool_item make_tool(const tool_entry &entry) {
    native::tool_item item;
    item.id = entry.command;
    item.kind = entry.kind;
    if (entry.kind != native::tool_kind::separator)
        item.icon = tool_icon(entry.command);
    return item;
}

// Join toolbar fields at a single right border; the bar owns horizontal rules.
native::tool_item make_control(const char *id, native::wnd &control,
                               int length) {
    native::tool_item item;
    item.id = id;
    item.kind = native::tool_kind::control;
    item.control = &control;
    item.length = length;
    item.control_fill_width = true;
    control.set_border_sides(native::border_sides::right);
    return item;
}

// Change a tool only if it would change: Native repaints regardless.
void update_enabled(native::toolbar &bar, const std::string &id, bool value) {
    const auto &items = bar.get_items();
    const auto item = std::ranges::find(items, id, &native::tool_item::id);
    if (item != items.end() && item->enabled != value)
        bar.set_enabled(id, value);
}

} // namespace

main_window::main_window(const app::options &settings)
    : native::app_wnd("Blaise", 60, 40, 1000, 760), settings_(settings),
      search_box_("", native::text_edit_mode::single_line, 0, 0, search_length, 24),
      show_box_({"All", "Unread"}, native::combo_box_style::drop_down_list, 0,
                0, 100, 24),
      sort_box_(sort_labels(), native::combo_box_style::drop_down_list, 0, 0,
                110, 24),
      summary_(model_), right_(summary_, message_pane_,
                               native::split_orientation::vertical, 0, 0, 760,
                               600),
      split_(folders_, right_, native::split_orientation::horizontal, 0, 0,
             1000, 600),
      toolbar_(*this), search_bar_(*this),
      status_(*this), open_dialog_(*this, "Open"),
      save_dialog_(*this, "Save As"), input_(*this), choice_(*this),
      account_window_(*this,
                      [this](prefs::account_list list) {
                          core_.set_accounts(std::move(list));
                      }),
      preferences_window_(*this,
                          [this](prefs::common values) {
                              core_.set_preferences(std::move(values));
                              start_timer();
                          }),
      filter_window_(*this,
                     [this](std::vector<filter::rule> rules) {
                         core_.set_rules(std::move(rules));
                     }),
      address_window_(*this, books_, [this] { save_addresses(); }),
      log_window_(*this),
      jobs_([](std::function<void()> work) {
          native::app::post(std::move(work));
      }),
      core_(settings.where, jobs_, *this, settings.connect) {
    // The window frame and toolbar/status rules enclose the outer edges.
    folders_.set_border_sides(native::border_sides::right);
    summary_.set_border_sides(native::border_sides::left |
                             native::border_sides::bottom);
    message_pane_.set_border_sides(native::border_sides::left |
                                  native::border_sides::top);
    split_.set_ratio(0.22f).set_minimums(120, 300);
    right_.set_ratio(0.45f).set_minimums(80, 80);
    build_menu();
    build_toolbar();
    build_search_bar();
    connect_panes();
    open_dialog_.add_filter({"All files", {"*"}});
    open_dialog_.on_modal_close.connect([this](native::dialog_result result) {
        if (result == native::dialog_result::accepted && open_action_)
            open_action_(open_dialog_.get_path());
        return true;
    });
    save_dialog_.on_modal_close.connect([this](native::dialog_result result) {
        if (result == native::dialog_result::accepted && save_action_)
            save_action_(save_dialog_.get_path());
        return true;
    });

    on_wnd_create.connect([this] {
        const auto appearance = native::theme::create(get_gpx());
        toolbar_.set_extent(appearance->get_toolbar_height());
        size_toolbar_separators(toolbar_);
        search_bar_.set_extent(appearance->get_toolbar_height());
        status_.set_extent(appearance->get_status_bar_height());
        split_.set_parent(this);
        split_.create();
        split_.show();
        place_children();
        books_.load(settings_.where.config / "addrbook");
        core_.load();
        core_.refresh_tree();
        const prefs::account_list accounts = core_.accounts();
        message_pane_.show_note(
            "Blaise\n\nChoose a folder to read mail. To add a mail account, "
            "use Tools, Accounts.\n");
        update_state();
        start_timer();
        return true;
    });
    on_menu.connect([this](int id) {
        const auto index = static_cast<std::size_t>(id - 1);
        if (index < menu_commands_.size())
            run(menu_commands_[index]);
        return true;
    });
}

main_window::~main_window() {
    timer_ = {};
    jobs_.drain();
}

void main_window::request_close() {
    if (!composers_.empty() &&
        native::message_box::show(
            *this, "Messages are still being written. Quit anyway?", "Blaise",
            native::message_box_buttons::yes_no,
            native::message_box_icon::question) !=
            native::message_box_result::yes)
        return;
    core_.shut_down();
    jobs_.drain();
    destroy();
}

void main_window::place_children() {
    split_.set_bounds(get_client_bounds());
}

void main_window::on_bounds_changed() {
    native::app_wnd::on_bounds_changed();
    place_children();
}

void main_window::build_toolbar() {
    for (const tool_entry &entry : tools)
        toolbar_.add_item(make_tool(entry));
    toolbar_.on_command.connect([this](native::tool_command command) {
        run(command.id);
        return true;
    });
}

void main_window::build_search_bar() {
    search_bar_.add_item(make_control("searchBox", search_box_, search_length));
    search_bar_.add_item(make_control("showBox", show_box_, 100));
    search_bar_.add_item(make_control("sortBox", sort_box_, 110));
    native::tool_item threads = make_tool(
        {"threadView", native::tool_kind::toggle});
    search_bar_.add_item(std::move(threads));
    show_box_.set_selected_index(0);
    sort_box_.set_selected_index(0);
    search_box_.on_change.connect([this](std::string) {
        apply_quick_search();
        return true;
    });
    show_box_.on_selection_change.connect([this](int) {
        if (!updating_)
            apply_view_filter();
        return true;
    });
    sort_box_.on_selection_change.connect([this](int index) {
        if (!updating_ && index >= 0)
            set_sort(sort_choices[static_cast<std::size_t>(index)].key);
        return true;
    });
    search_bar_.on_command.connect([this](native::tool_command command) {
        run(command.id);
        return true;
    });
}

void main_window::connect_panes() {
    folders_.on_selection_change.connect([this](native::tree_item_id) {
        open_selected_folder();
        return true;
    });
    summary_.on_selection_change.connect(
        [this](const std::vector<native::table_row_id> &) {
            show_selection();
            return true;
        });
    summary_.on_row_activate.connect([this](native::table_row_id) {
        if (folder_type_ == mail::item_type::draft ||
            folder_type_ == mail::item_type::queue)
            edit_selected();
        return true;
    });
    summary_.on_sort_request.connect([this](native::table_sort sort) {
        sort_by_column(sort);
        return true;
    });
    message_pane_.on_link = [this](const std::string &target) {
        auto writer = native::clipboard::open_write();
        writer.write_text(target);
        writer.commit();
        set_message("Link copied: " + target);
    };
    message_pane_.on_attachment = [this](std::size_t index) {
        save_attachment(index);
    };
}

void main_window::set_message(const std::string &text) {
    message_ = text;
    update_state();
}

void main_window::update_tools() {
    const bool has_mail = !summary_.get_selected_rows().empty();
    for (const char *id : {"reply", "replyAll", "forward", "deleteMail",
                           "markMessage"})
        update_enabled(toolbar_, id, has_mail);
    if (search_bar_.get_checked("threadView") != model_.threaded())
        search_bar_.set_checked("threadView", model_.threaded());
}

void main_window::update_state() {
    int unread = 0;
    for (const mail::msg_info &info : model_.messages())
        unread += info.has(mail::flag_unread) ? 1 : 0;
    const std::size_t selected = summary_.get_selected_rows().size();
    const app::folder_node *folder = folders_.find_node(folder_id_);
    int queued = 0;
    for (const app::folder_node &store : folders_.snapshot())
        for (const app::folder_node &child : store.children)
            if (child.type == mail::item_type::queue)
                queued += child.total_count;
    status_.set_parts(
        {{std::to_string(selected) + " of " +
              std::to_string(model_.rows().size()),
          130},
         {folder != nullptr
              ? std::to_string(unread) + " unread in " + folder_name(*folder)
              : std::string("No folder"),
          150},
         {"Queue " + std::to_string(queued), 70},
         {message_, 0}});
    const std::string title =
        folder != nullptr ? folder_name(*folder) + " - Blaise"
                          : std::string("Blaise");
    if (get_title() != title)
        set_title(title);
    update_tools();
}

void main_window::folders_changed(const std::vector<app::folder_node> &tree) {
    folders_.show_tree(tree);
    if (settings_.select) {
        // A remote folder may only appear in a later snapshot.
        std::string id = *settings_.select;
        std::optional<std::uint32_t> number;
        const std::size_t colon = id.rfind(':');
        if (colon != std::string::npos && id.find('#', colon) == id.npos)
            if (const auto value = util::parse_int(id.substr(colon + 1))) {
                number = static_cast<std::uint32_t>(*value);
                id.resize(colon);
            }
        if (folders_.select_id(id)) {
            settings_.select.reset();
            pending_select_ = number;
            open_selected_folder();
        }
    }
    update_state();
}

void main_window::folder_loaded(const app::folder_view &view) {
    if (view.id != folder_id_)
        return;
    const std::vector<std::uint32_t> selected = summary_.selected_numbers();
    folder_type_ = view.type;
    updating_ = true;
    model_.set_sort(view.sort, view.sort_ascending);
    model_.set_threaded(view.threaded);
    model_.set_messages(view.messages);
    const auto choice = std::ranges::find(sort_choices, view.sort,
                                          &sort_choice::key);
    if (choice != sort_choices.end())
        sort_box_.set_selected_index(
            static_cast<int>(choice - sort_choices.begin()));
    updating_ = false;
    summary_.refresh();
    if (pending_select_) {
        summary_.select_number(*pending_select_);
        pending_select_.reset();
        show_selection();
    } else if (selected.empty() || !model_.row_of(selected.front())) {
        if (shown_number_ != 0 && !model_.row_of(shown_number_)) {
            shown_number_ = 0;
            message_pane_.clear();
        }
    }
    update_state();
}

void main_window::status(const std::string &text) { set_message(text); }

void main_window::failure(const std::string &text) {
    set_message(text);
}

void main_window::mail_received(int count) {
    set_message(count == 0  ? "No new mail"
                : count == 1 ? "1 new message"
                             : std::to_string(count) + " new messages");
    if (count > 0 && !folder_id_.empty())
        core_.open_folder(folder_id_);
}

void main_window::save_addresses() {
    books_.save(settings_.where.config / "addrbook");
}

void main_window::start_timer() {
    timer_ = {};
    const prefs::common values = core_.preferences();
    if (!values.auto_check)
        return;
    const auto wait = std::chrono::minutes(values.check_minutes);
    timer_ = std::jthread([this, wait](std::stop_token stop) {
        std::mutex lock;
        std::condition_variable_any sleeper;
        while (true) {
            std::unique_lock hold(lock);
            if (sleeper.wait_for(hold, stop, wait,
                                 [&] { return stop.stop_requested(); }))
                return;
            native::app::post([this] { core_.receive_all(); });
        }
    });
}

} // namespace ui
