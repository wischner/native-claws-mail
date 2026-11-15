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
      auto_check_("Check for mail every"), startup_check_("Check for mail on startup"),
      auto_wrap_("Wrap messages at"), save_sent_("Keep a copy of sent mail"),
      mark_read_("Mark mail read when opened"), threaded_("Thread new folders"),
      empty_trash_("Empty the trash on exit") {
    add_caption("minutes", 0, 0);
    add_caption("columns", 0, 0);
    add_caption("Quote mark", 0, 0);
}

void preferences_window::populate() {
    for (native::wnd *child : std::initializer_list<native::wnd *>{
             &auto_check_, &minutes_, &startup_check_, &auto_wrap_, &wrap_,
             &quote_mark_, &save_sent_, &mark_read_, &threaded_,
             &empty_trash_})
        attach(*child);
}

int preferences_window::layout() {
    const int h = field_height();
    const int x = std::max(220, native::font_t::stock(native::font_role::control)
        .measure_text("Check for mail every").width + 44);
    const int width = get_client_bounds().w() - 24;
    auto_check_.set_bounds(native::rect(12, row_y(0), x - 20, h));
    minutes_.set_bounds(native::rect(x, row_y(0), 64, h));
    startup_check_.set_bounds(native::rect(12, row_y(1), width, h));
    auto_wrap_.set_bounds(native::rect(12, row_y(2), x - 20, h));
    wrap_.set_bounds(native::rect(x, row_y(2), 64, h));
    quote_mark_.set_bounds(native::rect(x, row_y(3), 64, h));
    caption_bounds(0, x + 72, row_y(0), width - x - 60);
    caption_bounds(1, x + 72, row_y(2), width - x - 60);
    caption_bounds(2, 34, row_y(3), x - 42);
    int row = 4;
    for (auto *field : {&save_sent_, &mark_read_, &threaded_, &empty_trash_})
        field->set_bounds(native::rect(12, row_y(row++), width, h));
    return row_y(7) + h;
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
    if (!minutes || *minutes < 1 || *minutes > 10080 ||
        !wrap || *wrap < 20 || *wrap > 998) {
        native::message_box::show(*this, "Enter whole numbers: at least one "
                                         "minute (up to 10080) and 20–998 columns.",
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
      add_("Add"), replace_("Replace"), remove_("Delete"), up_("Up"), down_("Down") {
    add_caption("Example: subject matchcase \"report\" mark_as_read", 0, 0);
    rows_.on_selection_change.connect([this](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < rules_.size())
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

int filter_window::layout() {
    const int h = field_height();
    const int width = get_client_bounds().w() - 24;
    rows_.set_bounds(native::rect(12, 12, width, 210));
    caption_bounds(0, 12, 230, width);
    text_.set_bounds(native::rect(12, 238 + h, width, h));
    int x = 12;
    for (auto *button : {&add_, &replace_, &remove_, &up_, &down_}) {
        button->set_bounds(native::rect(x, 246 + 2 * h, 88, h));
        x += 96;
    }
    return 246 + 3 * h;
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
    } else {
        native::message_box::show(*this, "That rule could not be understood.",
            "Filter Rules", native::message_box_buttons::ok, native::message_box_icon::warning);
    }
}

void filter_window::remove_rule() {
    const int index = rows_.get_selected_index();
    if (index < 0)
        return;
    rules_.erase(rules_.begin() + index);
    text_.set_text("");
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
    if (!util::trim(text_.get_text()).empty()) {
        const auto parsed = filter::parse_rule(text_.get_text());
        if (!parsed) {
            native::message_box::show(*this, "That rule could not be understood.",
                "Filter Rules", native::message_box_buttons::ok, native::message_box_icon::warning);
            return false;
        }
        const int index = rows_.get_selected_index();
        if (index >= 0 && static_cast<std::size_t>(index) < rules_.size())
            rules_[static_cast<std::size_t>(index)] = *parsed;
        else rules_.push_back(*parsed);
    }
    if (done_) {
        const auto done = done_;
        const auto copy = rules_;
        native::app::post([done, copy] { done(copy); });
    }
    return true;
}

address_window::address_window(native::app_wnd &owner, addr::book_list &books,
                               std::function<bool()> changed)
    : native::modeless_wnd(owner, "Address Book", 160, 120, 560, 460),
      books_(books), changed_(std::move(changed)),
      add_("New"), save_("Save"), remove_("Delete"), import_("Import..."),
      import_dialog_(*this, "Import Addresses") {
    tab_sequence({&rows_, &name_, &email_, &add_, &save_, &remove_, &import_});
    on_wnd_resize.connect([this](native::size) {
        if (built_) place_children();
        return true;
    });
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
        event.g.draw_text("Name", native::rect(12, name_.get_bounds().y1(), 80, form_control_height(*this)), layout);
        event.g.draw_text("Address", native::rect(12, email_.get_bounds().y1(), 80, form_control_height(*this)), layout);
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
    if (!get_created()) {
        built_ = false;
        create();
    }
    if (!built_) {
        for (native::wnd *child : std::initializer_list<native::wnd *>{
                 &rows_, &name_, &email_, &add_, &save_, &remove_, &import_}) {
            child->set_parent(this);
            child->create();
            child->show();
        }
        built_ = true;
    }
    place_children();
    name_.set_text("");
    email_.set_text("");
    show_rows();
    center_to_parent();
    show();
    rows_.focus();
    invalidate();
}

void address_window::place_children() {
    const int h = form_control_height(*this);
    const int width = get_client_bounds().w() - 24;
    const int list_height = std::max(100, int(get_client_bounds().h()) - 3 * h - 56);
    rows_.set_bounds(native::rect(12, 12, width, list_height));
    name_.set_bounds(native::rect(100, 20 + list_height, width - 88, h));
    email_.set_bounds(native::rect(100, 28 + list_height + h, width - 88, h));
    int x = 12;
    for (auto *button : {&add_, &save_, &remove_, &import_}) {
        button->set_bounds(native::rect(x, 36 + list_height + 2 * h, 96, h));
        x += 104;
    }
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
    const auto previous = books_;
    addr::person item;
    item.common_name = name_.get_text();
    item.emails.push_back({"", "", email_.get_text(), ""});
    if (util::trim(email_.get_text()).empty())
        return;
    books_.primary().add(std::move(item));
    show_rows();
    rows_.set_selected_index(
        static_cast<int>(books_.primary().people().size()) - 1);
    if (!changed_()) { books_ = previous; show_rows(); }
}

void address_window::store_person() {
    const auto previous = books_;
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
    if (!changed_()) { books_ = previous; show_rows(); }
}

void address_window::remove_person() {
    const auto previous = books_;
    const int index = rows_.get_selected_index();
    auto &people = books_.primary().people();
    if (index < 0 || static_cast<std::size_t>(index) >= people.size())
        return;
    books_.primary().remove_person(
        people[static_cast<std::size_t>(index)].uid);
    show_rows();
    if (!changed_()) { books_ = previous; show_rows(); }
}

void address_window::import_file() {
    const auto previous = books_;
    const std::filesystem::path path = import_dialog_.get_path();
    const auto text = util::read_file(path);
    if (!text) {
        native::message_box::show(*this, "Could not read " + path.string(),
            "Import Addresses", native::message_box_buttons::ok, native::message_box_icon::warning);
        return;
    }
    const bool card = util::iends_with(path.string(), ".vcf");
    for (addr::person &item :
         card ? addr::import_vcard(*text) : addr::import_ldif(*text))
        books_.primary().add(std::move(item));
    show_rows();
    if (!changed_()) { books_ = previous; show_rows(); }
}

log_window::log_window(native::app_wnd &owner)
    : native::modeless_wnd(owner, "Network Log", 140, 100, 640, 420),
      bar_(*this) {
    text_.set_tab_order(1);
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
        bar_.set_extent(native::theme::create(get_gpx())->get_toolbar_height());
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
    text_.focus();
}

} // namespace ui
