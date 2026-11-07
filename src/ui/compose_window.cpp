//
// The message editor.
//
// Derived from Claws Mail src/compose.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "compose_window.h"

#include <algorithm>
#include <array>

#include "mime/address.h"
#include "mime/builder.h"
#include "tool_icons.h"
#include "util/text.h"

namespace ui {
namespace {

constexpr int bar_extent = 30;
constexpr int label_width = 64;
constexpr int row_height = 28;
constexpr int margin = 8;
constexpr int files_height = 56;

// One tool.
struct tool_entry {
    const char *command;
    native::tool_kind kind;
};

constexpr std::array<tool_entry, 12> tools{{
    {"sendMail", native::tool_kind::button},
    {"fileSave", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"attachFile", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"undo", native::tool_kind::button},
    {"redo", native::tool_kind::button},
    {"", native::tool_kind::separator},
    {"cut", native::tool_kind::button},
    {"copy", native::tool_kind::button},
    {"paste", native::tool_kind::button},
    {"addressBook", native::tool_kind::button},
}};

// Return the typed fragment after the last comma.
std::string last_fragment(const std::string &text) {
    const std::size_t comma = text.rfind(',');
    return std::string(util::trim(
        comma == std::string::npos ? text : text.substr(comma + 1)));
}

// Return text with its last fragment replaced by a finished address.
std::string with_completion(const std::string &text,
                            const std::string &address) {
    const std::size_t comma = text.rfind(',');
    if (comma == std::string::npos)
        return address + ", ";
    return text.substr(0, comma + 1) + " " + address + ", ";
}

// Return the sending choices.
std::vector<std::string> account_labels(const prefs::account_list &accounts) {
    std::vector<std::string> labels;
    for (const prefs::account &item : accounts.all())
        labels.push_back(item.from_header());
    return labels;
}

} // namespace

compose_window::compose_window(native::app_wnd &owner, app::engine &core,
                               const addr::book_list &books,
                               compose::draft text,
                               std::function<void(compose_window *)> closed)
    : native::modeless_wnd(owner, "Compose", 120, 80, 740, 620), core_(core),
      books_(books), accounts_(core.accounts()), draft_(std::move(text)),
      closed_(std::move(closed)),
      from_(account_labels(accounts_),
            native::combo_box_style::drop_down_list, 0, 0, 600, 24),
      to_({}, native::combo_box_style::editable, 0, 0, 600, 24),
      cc_({}, native::combo_box_style::editable, 0, 0, 600, 24),
      bcc_("", native::text_edit_mode::single_line, 0, 0, 600, 24),
      subject_("", native::text_edit_mode::single_line, 0, 0, 600, 24),
      body_("", 0, 0, 600, 300), files_({}, 0, 0, 600, files_height),
      toolbar_(*this, native::window_edge::top, bar_extent), status_(*this),
      attach_dialog_(*this, "Attach File") {
    build_toolbar();
    body_.set_show_line_numbers(false);
    attach_dialog_.add_filter({"All files", {"*"}});
    attach_dialog_.on_modal_close.connect([this](native::dialog_result result) {
        if (result == native::dialog_result::accepted) {
            const std::filesystem::path path = attach_dialog_.get_path();
            draft_.attachments.push_back(
                {path, path.filename().string(),
                 mime::guess_mime_type(path), ""});
            dirty_ = true;
            update_attachments();
        }
        return true;
    });
    const auto mark_dirty = [this] {
        if (!updating_)
            dirty_ = true;
    };
    subject_.on_change.connect([this, mark_dirty](std::string) {
        mark_dirty();
        update_title();
        return true;
    });
    bcc_.on_change.connect([mark_dirty](std::string) {
        mark_dirty();
        return true;
    });
    body_.on_text_change.connect([mark_dirty] {
        mark_dirty();
        return true;
    });
    to_.on_text_change.connect([this](std::string typed) {
        update_completion(to_, typed);
        return true;
    });
    cc_.on_text_change.connect([this](std::string typed) {
        update_completion(cc_, typed);
        return true;
    });
    const auto picked = [this](native::combo_box &field, int index) {
        if (updating_ || index < 0)
            return;
        const std::string choice =
            field.get_items()[static_cast<std::size_t>(index)];
        updating_ = true;
        field.set_text(with_completion(field.get_text(), choice));
        updating_ = false;
    };
    to_.on_selection_change.connect([this, picked](int index) {
        picked(to_, index);
        return true;
    });
    cc_.on_selection_change.connect([this, picked](int index) {
        picked(cc_, index);
        return true;
    });
    on_wnd_create.connect([this] {
        status_.set_extent(
            native::theme::create(get_gpx())->get_status_bar_height());
        for (native::wnd *child :
             std::initializer_list<native::wnd *>{
                 &from_, &to_, &cc_, &bcc_, &subject_, &body_, &files_}) {
            child->set_parent(this);
            child->create();
            child->show();
        }
        updating_ = true;
        const auto account = std::ranges::find(accounts_.all(),
                                               draft_.account_id,
                                               &prefs::account::id);
        if (account != accounts_.all().end())
            from_.set_selected_index(static_cast<int>(
                account - accounts_.all().begin()));
        else if (!accounts_.all().empty())
            from_.set_selected_index(0);
        to_.set_text(draft_.to);
        cc_.set_text(draft_.cc);
        bcc_.set_text(draft_.bcc);
        subject_.set_text(draft_.subject);
        body_.set_text(draft_.body);
        body_.go_to_offset(std::min(draft_.cursor, draft_.body.size()));
        updating_ = false;
        update_attachments();
        update_title();
        place_children();
        return true;
    });
    on_wnd_resize.connect([this](native::size) {
        place_children();
        return true;
    });
    on_wnd_paint.connect([this](native::wnd_paint_event event) {
        auto appearance = native::theme::create(event.g);
        event.g.set_ink(appearance->get_button_foreground_color());
        event.g.set_font(native::font_t::stock(native::font_role::control));
        native::text_layout layout;
        layout.vertical = native::text_valign::center;
        const native::rect client = get_client_bounds();
        int y = client.p.y + margin;
        for (const char *label : {"From:", "To:", "Cc:", "Bcc:", "Subject:"}) {
            event.g.draw_text(
                label,
                native::rect(margin, static_cast<native::coord>(y),
                             label_width, 24),
                layout);
            y += row_height;
        }
        return true;
    });
}

void compose_window::build_toolbar() {
    for (const tool_entry &entry : tools) {
        native::tool_item item;
        item.id = entry.command;
        item.kind = entry.kind;
        if (entry.kind != native::tool_kind::separator)
            item.icon = tool_icon(entry.command);
        toolbar_.add_item(std::move(item));
    }
    toolbar_.on_command.connect([this](native::tool_command command) {
        run(command.id);
        return true;
    });
}

void compose_window::place_children() {
    const native::rect client = get_client_bounds();
    const int x = client.p.x + margin * 2 + label_width;
    const int width =
        std::max(120, static_cast<int>(client.d.w) - label_width -
                          margin * 3);
    int y = client.p.y + margin;
    for (native::wnd *field : std::initializer_list<native::wnd *>{
             &from_, &to_, &cc_, &bcc_, &subject_}) {
        field->set_bounds(native::rect(static_cast<native::coord>(x),
                                       static_cast<native::coord>(y),
                                       static_cast<native::dim>(width), 24));
        y += row_height;
    }
    const bool has_files = !draft_.attachments.empty();
    const int bottom = client.p.y + static_cast<int>(client.d.h) - margin;
    const int body_bottom =
        has_files ? bottom - files_height - margin : bottom;
    body_.set_bounds(native::rect(
        client.p.x, static_cast<native::coord>(y + margin / 2),
        client.d.w,
        static_cast<native::dim>(std::max(60, body_bottom - y))));
    files_.set_bounds(native::rect(
        client.p.x + margin,
        static_cast<native::coord>(has_files ? bottom - files_height
                                             : bottom + 100),
        static_cast<native::dim>(client.d.w - 2 * margin), files_height));
}

void compose_window::run(std::string_view command) {
    if (command == "sendMail")
        send();
    else if (command == "fileSave")
        save();
    else if (command == "attachFile")
        attach_file();
    else if (command == "undo")
        body_.undo();
    else if (command == "redo")
        body_.redo();
    else if (command == "cut")
        body_.cut();
    else if (command == "copy")
        body_.copy();
    else if (command == "paste")
        body_.paste();
}

compose::draft compose_window::current() const {
    compose::draft out = draft_;
    const int index = from_.get_selected_index();
    if (index >= 0 &&
        static_cast<std::size_t>(index) < accounts_.all().size()) {
        const prefs::account &item =
            accounts_.all()[static_cast<std::size_t>(index)];
        out.account_id = item.id;
        out.from = item.from_header();
    }
    const auto strip = [](std::string value) {
        std::string_view text = util::trim(value);
        while (text.ends_with(','))
            text = util::trim(text.substr(0, text.size() - 1));
        return std::string(text);
    };
    out.to = strip(to_.get_text());
    out.cc = strip(cc_.get_text());
    out.bcc = strip(bcc_.get_text());
    out.subject = subject_.get_text();
    out.body = body_.get_text();
    return out;
}

bool compose_window::has_recipients() const {
    return !current().recipients().empty();
}

void compose_window::send() {
    if (accounts_.all().empty()) {
        native::message_box::show(*this, "Set up an account first.",
                                  "Blaise", native::message_box_buttons::ok,
                                  native::message_box_icon::warning);
        return;
    }
    if (!has_recipients()) {
        native::message_box::show(*this, "Add at least one recipient.",
                                  "Blaise", native::message_box_buttons::ok,
                                  native::message_box_icon::warning);
        return;
    }
    if (subject_.get_text().empty() &&
        native::message_box::show(*this, "Send this message with no subject?",
                                  "Blaise", native::message_box_buttons::yes_no,
                                  native::message_box_icon::question) !=
            native::message_box_result::yes)
        return;
    core_.send(current());
    finish();
}

void compose_window::save() {
    core_.save_draft(current());
    dirty_ = false;
}

void compose_window::attach_file() { attach_dialog_.show(); }

void compose_window::update_title() {
    const std::string &subject = subject_.get_text();
    set_title((subject.empty() ? std::string("Compose") : subject) +
              " - Blaise");
}

void compose_window::update_attachments() {
    std::vector<std::string> names;
    for (const compose::attachment &item : draft_.attachments)
        names.push_back(item.name);
    files_.set_items(names);
    status_.set_parts({{std::to_string(names.size()) + " attached", 130},
                       {"", 0}});
    place_children();
}

void compose_window::update_completion(native::combo_box &field,
                                       const std::string &text) {
    if (updating_)
        return;
    dirty_ = true;
    std::vector<std::string> items;
    for (const addr::completion &match : books_.complete(last_fragment(text)))
        items.push_back(match.formatted());
    updating_ = true;
    field.set_items(std::move(items));
    field.set_text(text);
    updating_ = false;
}

void compose_window::request_close() {
    if (dirty_) {
        const auto answer = native::message_box::show(
            *this, "Save this message as a draft?", "Blaise",
            native::message_box_buttons::yes_no_cancel,
            native::message_box_icon::question);
        if (answer == native::message_box_result::cancel ||
            answer == native::message_box_result::none)
            return;
        if (answer == native::message_box_result::yes)
            save();
    }
    finish();
}

void compose_window::finish() {
    // The owner deletes this object; do it after the signal stack unwinds.
    const auto closed = closed_;
    compose_window *self = this;
    native::app::post([closed, self] { closed(self); });
}

} // namespace ui
