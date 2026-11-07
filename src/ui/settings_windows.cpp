//
// The smaller settings dialogs.
//
// Derived from Claws Mail src/prefs_common.c, src/prefs_filtering.c,
// src/addressbook.c and src/gtk/logwindow.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "settings_windows.h"

#include <algorithm>

#include "mime/address.h"
#include "tool_icons.h"
#include "util/files.h"
#include "util/log.h"
#include "util/text.h"

namespace ui {
namespace {

using native::text_edit_mode;

} // namespace

preferences_window::preferences_window(
    native::app_wnd &owner, std::function<void(prefs::common)> done)
    : form_window(owner, "Preferences", 440, 380), done_(std::move(done)),
      auto_check_("Check for mail every", 12, 12, 200, 24),
      minutes_("", text_edit_mode::single_line, 220, 12, 60, 26),
      startup_check_("Check for mail on startup", 12, 44, 300, 24),
      auto_wrap_("Wrap messages at", 12, 76, 200, 24),
      wrap_("", text_edit_mode::single_line, 220, 76, 60, 26),
      quote_mark_("", text_edit_mode::single_line, 220, 108, 60, 26),
      save_sent_("Keep a copy of sent mail", 12, 140, 300, 24),
      mark_read_("Mark mail read when opened", 12, 172, 300, 24),
      threaded_("Thread new folders", 12, 204, 300, 24),
      empty_trash_("Empty the trash on exit", 12, 236, 300, 24) {
    add_caption("minutes", 288, 12);
    add_caption("columns", 288, 76);
    add_caption("Quote mark", 34, 108);
}

void preferences_window::populate() {
    for (native::wnd *child : std::initializer_list<native::wnd *>{
             &auto_check_, &minutes_, &startup_check_, &auto_wrap_, &wrap_,
             &quote_mark_, &save_sent_, &mark_read_, &threaded_,
             &empty_trash_})
        attach(*child);
}

void preferences_window::edit(prefs::common values) {
    values_ = std::move(values);
    open();
}

void preferences_window::prepare() {
    auto_check_.set_checked(values_.auto_check);
    minutes_.set_text(std::to_string(values_.check_minutes));
    startup_check_.set_checked(values_.check_on_startup);
    auto_wrap_.set_checked(values_.auto_wrap);
    wrap_.set_text(std::to_string(values_.wrap_length));
    quote_mark_.set_text(values_.quote_mark);
    save_sent_.set_checked(values_.save_message_to_sent);
    mark_read_.set_checked(values_.mark_read_on_open);
    threaded_.set_checked(values_.threaded_by_default);
    empty_trash_.set_checked(values_.empty_trash_on_exit);
}

bool preferences_window::accept() {
    const auto minutes = util::parse_int(util::trim(minutes_.get_text()));
    const auto wrap = util::parse_int(util::trim(wrap_.get_text()));
    if (!minutes || *minutes < 1 || !wrap || *wrap < 20) {
        native::message_box::show(*this, "Enter whole numbers: at least one "
                                         "minute and twenty columns.",
                                  "Preferences",
                                  native::message_box_buttons::ok,
                                  native::message_box_icon::warning);
        return false;
    }
    values_.auto_check = auto_check_.get_checked();
    values_.check_minutes = static_cast<int>(*minutes);
    values_.check_on_startup = startup_check_.get_checked();
    values_.auto_wrap = auto_wrap_.get_checked();
    values_.wrap_length = static_cast<int>(*wrap);
    values_.quote_mark = quote_mark_.get_text();
    values_.save_message_to_sent = save_sent_.get_checked();
    values_.mark_read_on_open = mark_read_.get_checked();
    values_.threaded_by_default = threaded_.get_checked();
    values_.empty_trash_on_exit = empty_trash_.get_checked();
    if (done_) {
        const auto done = done_;
        const prefs::common copy = values_;
        native::app::post([done, copy] { done(copy); });
    }
    return true;
}

filter_window::filter_window(
    native::app_wnd &owner,
    std::function<void(std::vector<filter::rule>)> done)
    : form_window(owner, "Filter Rules", 640, 420), done_(std::move(done)),
      rows_({}, 12, 12, 616, 240),
      text_("", text_edit_mode::single_line, 12, 284, 616, 26),
      add_("Add", 12, 318, 80, 28), replace_("Replace", 98, 318, 80, 28),
      remove_("Delete", 184, 318, 80, 28), up_("Up", 270, 318, 64, 28),
      down_("Down", 340, 318, 64, 28) {
    add_caption("Rule, such as: subject matchcase \"report\" move "
                "\"#mh/Mailbox/work\"",
                12, 258);
    rows_.on_selection_change.connect([this](int index) {
        if (index >= 0)
            text_.set_text(
                rules_[static_cast<std::size_t>(index)].to_string());
        return true;
    });
    add_.on_click.connect([this] {
        add_rule();
        return true;
    });
    replace_.on_click.connect([this] {
        replace_rule();
        return true;
    });
    remove_.on_click.connect([this] {
        remove_rule();
        return true;
    });
    up_.on_click.connect([this] {
        move_rule(-1);
        return true;
    });
    down_.on_click.connect([this] {
        move_rule(1);
        return true;
    });
}

void filter_window::populate() {
    for (native::wnd *child : std::initializer_list<native::wnd *>{
             &rows_, &text_, &add_, &replace_, &remove_, &up_, &down_})
        attach(*child);
}

void filter_window::edit(std::vector<filter::rule> rules) {
    rules_ = std::move(rules);
    open();
}

void filter_window::prepare() {
    show_rows();
    text_.set_text("");
}

void filter_window::show_rows() {
    std::vector<std::string> labels;
    for (const filter::rule &item : rules_)
        labels.push_back(item.to_string());
    rows_.set_items(std::move(labels));
}

void filter_window::add_rule() {
    if (auto parsed = filter::parse_rule(text_.get_text())) {
        rules_.push_back(std::move(*parsed));
        show_rows();
        rows_.set_selected_index(static_cast<int>(rules_.size()) - 1);
    } else {
        native::message_box::show(*this, "That rule could not be understood.",
                                  "Filter Rules",
                                  native::message_box_buttons::ok,
                                  native::message_box_icon::warning);
    }
}

void filter_window::replace_rule() {
    const int index = rows_.get_selected_index();
    if (index < 0)
        return;
    if (auto parsed = filter::parse_rule(text_.get_text())) {
        rules_[static_cast<std::size_t>(index)] = std::move(*parsed);
        show_rows();
        rows_.set_selected_index(index);
    }
}

void filter_window::remove_rule() {
    const int index = rows_.get_selected_index();
    if (index < 0)
        return;
    rules_.erase(rules_.begin() + index);
    show_rows();
}

void filter_window::move_rule(int by) {
    const int index = rows_.get_selected_index();
    const int target = index + by;
    if (index < 0 || target < 0 ||
        target >= static_cast<int>(rules_.size()))
        return;
    std::swap(rules_[static_cast<std::size_t>(index)],
              rules_[static_cast<std::size_t>(target)]);
    show_rows();
    rows_.set_selected_index(target);
}

bool filter_window::accept() {
    if (done_) {
        const auto done = done_;
        const auto copy = rules_;
        native::app::post([done, copy] { done(copy); });
    }
    return true;
}

address_window::address_window(native::app_wnd &owner, addr::book_list &books,
                               std::function<void()> changed)
    : native::modeless_wnd(owner, "Address Book", 160, 120, 560, 460),
      books_(books), changed_(std::move(changed)), rows_({}, 12, 12, 536, 300),
      name_("", text_edit_mode::single_line, 80, 320, 468, 26),
      email_("", text_edit_mode::single_line, 80, 352, 468, 26),
      add_("New", 12, 388, 80, 28), save_("Save", 98, 388, 80, 28),
      remove_("Delete", 184, 388, 80, 28), import_("Import...", 270, 388, 88,
                                                   28),
      import_dialog_(*this, "Import Addresses") {
    import_dialog_.add_filter({"Address files", {"*.ldif", "*.vcf"}});
    import_dialog_.add_filter({"All files", {"*"}});
    import_dialog_.on_modal_close.connect(
        [this](native::dialog_result result) {
            if (result == native::dialog_result::accepted)
                import_file();
            return true;
        });
    on_wnd_paint.connect([this](native::wnd_paint_event event) {
        auto appearance = native::theme::create(event.g);
        event.g.set_ink(appearance->get_button_foreground_color());
        event.g.set_font(native::font_t::stock(native::font_role::control));
        native::text_layout layout;
        layout.vertical = native::text_valign::center;
        event.g.draw_text("Name", native::rect(12, 320, 64, 24), layout);
        event.g.draw_text("Address", native::rect(12, 352, 64, 24), layout);
        return true;
    });
    rows_.on_selection_change.connect([this](int) {
        show_person();
        return true;
    });
    add_.on_click.connect([this] {
        add_person();
        return true;
    });
    save_.on_click.connect([this] {
        store_person();
        return true;
    });
    remove_.on_click.connect([this] {
        remove_person();
        return true;
    });
    import_.on_click.connect([this] {
        import_dialog_.show();
        return true;
    });
}

void address_window::open() {
    if (!get_created())
        create();
    if (!built_) {
        for (native::wnd *child : std::initializer_list<native::wnd *>{
                 &rows_, &name_, &email_, &add_, &save_, &remove_, &import_}) {
            child->set_parent(this);
            child->create();
            child->show();
        }
        built_ = true;
    }
    show_rows();
    center_to_parent();
    show();
    invalidate();
}

void address_window::show_rows() {
    std::vector<std::string> labels;
    for (const addr::person &item : books_.primary().people()) {
        const std::string email =
            item.emails.empty() ? std::string() : item.emails.front().address;
        labels.push_back(mime::format_mailbox({item.display_name(), email}));
    }
    rows_.set_items(std::move(labels));
}

void address_window::show_person() {
    const int index = rows_.get_selected_index();
    const auto &people = books_.primary().people();
    if (index < 0 || static_cast<std::size_t>(index) >= people.size())
        return;
    const addr::person &item = people[static_cast<std::size_t>(index)];
    name_.set_text(item.display_name());
    email_.set_text(item.emails.empty() ? std::string()
                                        : item.emails.front().address);
}

void address_window::add_person() {
    addr::person item;
    item.common_name = name_.get_text();
    item.emails.push_back({"", "", email_.get_text(), ""});
    if (util::trim(email_.get_text()).empty())
        return;
    books_.primary().add(std::move(item));
    show_rows();
    rows_.set_selected_index(
        static_cast<int>(books_.primary().people().size()) - 1);
    changed_();
}

void address_window::store_person() {
    const int index = rows_.get_selected_index();
    auto &people = books_.primary().people();
    if (index < 0 || static_cast<std::size_t>(index) >= people.size())
        return;
    addr::person &item = people[static_cast<std::size_t>(index)];
    item.common_name = name_.get_text();
    if (item.emails.empty())
        item.emails.push_back({"", "", "", ""});
    item.emails.front().address = email_.get_text();
    show_rows();
    rows_.set_selected_index(index);
    changed_();
}

void address_window::remove_person() {
    const int index = rows_.get_selected_index();
    auto &people = books_.primary().people();
    if (index < 0 || static_cast<std::size_t>(index) >= people.size())
        return;
    books_.primary().remove_person(
        people[static_cast<std::size_t>(index)].uid);
    show_rows();
    changed_();
}

void address_window::import_file() {
    const std::filesystem::path path = import_dialog_.get_path();
    const auto text = util::read_file(path);
    if (!text)
        return;
    const bool card = util::iends_with(path.string(), ".vcf");
    for (addr::person &item :
         card ? addr::import_vcard(*text) : addr::import_ldif(*text))
        books_.primary().add(std::move(item));
    show_rows();
    changed_();
}

log_window::log_window(native::app_wnd &owner)
    : native::modeless_wnd(owner, "Network Log", 140, 100, 640, 420),
      text_("", 0, 0, 640, 420), bar_(*this, native::window_edge::top, 30) {
    text_.set_show_line_numbers(false);
    text_.set_read_only(true);
    native::tool_item clear;
    clear.id = "cut";
    clear.icon = tool_icon("cut");
    bar_.add_item(std::move(clear));
    bar_.on_command.connect([this](native::tool_command) {
        util::log_clear();
        text_.set_text("");
        return true;
    });
    on_wnd_create.connect([this] {
        text_.set_parent(this);
        text_.create();
        text_.show();
        text_.set_bounds(get_client_bounds());
        return true;
    });
    on_wnd_resize.connect([this](native::size) {
        text_.set_bounds(get_client_bounds());
        return true;
    });
}

void log_window::open() {
    if (!get_created())
        create();
    std::string lines;
    for (const util::log_line &line : util::log_snapshot()) {
        switch (line.level) {
        case util::log_level::sent:
            lines += "> ";
            break;
        case util::log_level::received:
            lines += "< ";
            break;
        case util::log_level::warning:
            lines += "! ";
            break;
        case util::log_level::error:
            lines += "!! ";
            break;
        case util::log_level::info:
            lines += "* ";
            break;
        }
        lines += line.text + "\n";
    }
    text_.set_text(lines);
    center_to_parent();
    show();
}

} // namespace ui
