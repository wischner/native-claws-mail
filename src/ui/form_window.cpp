//
// Shared parts of the dialogs.
//
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "form_window.h"

#include <algorithm>

namespace ui {
namespace {

constexpr int button_width = 88;
constexpr int button_height = 30;
constexpr int margin = 12;

} // namespace

int form_control_height(native::wnd &window) {
    const auto appearance = native::theme::create(window.get_gpx());
    const int themed = appearance->get_form_control_height();
    // Native constrains single-line inputs to this exact height.
    return themed > 0 ? themed : appearance->defaults().text_edit_height;
}

void tab_sequence(std::initializer_list<native::wnd *> controls) {
    unsigned order = 0;
    for (auto *control : controls) control->set_tab_order(++order);
}

form_window::form_window(native::app_wnd &owner, const std::string &title,
                         int width, int height)
    : native::modal_wnd(owner, title, 200, 160,
                        static_cast<native::dim>(width),
                        static_cast<native::dim>(height)),
      ok_("OK", static_cast<native::coord>(width - 2 * button_width - margin -
                                           6),
          static_cast<native::coord>(height - button_height - margin),
          button_width, button_height),
      cancel_("Cancel",
              static_cast<native::coord>(width - button_width - margin),
              static_cast<native::coord>(height - button_height - margin),
              button_width, button_height), minimum_width_(width) {
    on_wnd_create.connect([this] {
        controls_.clear();
        populate();
        attach(ok_);
        attach(cancel_);
        populated_ = true;
        place_children();
        return true;
    });
    on_wnd_resize.connect([this](native::size) {
        if (populated_) place_children();
        return true;
    });
    on_wnd_paint.connect([this](native::wnd_paint_event event) {
        auto appearance = native::theme::create(event.g);
        event.g.set_ink(appearance->get_button_foreground_color());
        event.g.set_font(native::font_t::stock(native::font_role::control));
        native::text_layout layout;
        layout.vertical = native::text_valign::center;
        for (const caption &item : captions_)
            event.g.draw_text(item.text,
                              native::rect(static_cast<native::coord>(item.x),
                                           static_cast<native::coord>(item.y),
                                           item.width, field_height()),
                              layout);
        return true;
    });
    ok_.on_click.connect([this] {
        finish(true);
        return true;
    });
    cancel_.on_click.connect([this] {
        finish(false);
        return true;
    });
}

void form_window::add_caption(const std::string &text, int x, int y) {
    captions_.push_back({text, x, y});
}

void form_window::set_caption(std::size_t index, const std::string &text) {
    captions_.at(index).text = text;
    invalidate();
}

void form_window::attach(native::wnd &child) {
    child.set_tab_order(static_cast<unsigned>(controls_.size() + 1));
    controls_.push_back(&child);
    child.set_parent(this);
    child.create();
    child.show();
}

void form_window::open() {
    if (!get_created())
        create();
    prepare();
    center_to_parent();
    show();
    if (!controls_.empty()) controls_.front()->focus();
    invalidate();
}

void form_window::caption_bounds(std::size_t index, int x, int y, int width) {
    auto &caption = captions_.at(index);
    caption.x = x;
    caption.y = y;
    caption.width = width;
}

void form_window::place_children() {
    if (laying_out_) return;
    laying_out_ = true;
    if (get_dimensions().w < minimum_width_)
        set_dimensions(native::size(minimum_width_, get_dimensions().h));
    const int bottom = layout();
    const int height = bottom + 2 * margin + field_height();
    set_dimensions(native::size(get_dimensions().w, height));
    const int width = get_client_bounds().w();
    ok_.set_bounds(native::rect(width - 2 * button_width - margin - 8,
                               bottom + margin, button_width, field_height()));
    cancel_.set_bounds(native::rect(width - button_width - margin,
                                   bottom + margin, button_width, field_height()));
    laying_out_ = false;
    invalidate();
}

void form_window::finish(bool accepted) {
    if (accepted && !accept())
        return;
    close(accepted ? native::dialog_result::accepted
                   : native::dialog_result::cancelled);
}

input_window::input_window(native::app_wnd &owner)
    : form_window(owner, "Blaise", 380, 124),
      answer_("", native::text_edit_mode::single_line) {
    add_caption("", 12, 10);
}

void input_window::populate() { attach(answer_); }

int input_window::layout() {
    const int width = get_client_bounds().w() - 24;
    caption_bounds(0, 12, 12, width);
    answer_.set_bounds(native::rect(12, row_y(1), width, field_height()));
    return row_y(1) + field_height();
}

void input_window::ask(const std::string &title, const std::string &prompt,
                       const std::string &initial,
                       std::function<void(const std::string &)> done) {
    done_ = std::move(done);
    set_title(title);
    set_caption(0, prompt);
    if (!get_created())
        create();
    answer_.set_text(initial);
    open();
}

bool input_window::accept() {
    if (done_) {
        const auto done = done_;
        const std::string text = answer_.get_text();
        native::app::post([done, text] { done(text); });
    }
    return true;
}

choice_window::choice_window(native::app_wnd &owner)
    : form_window(owner, "Blaise", 380, 340), rows_() {}

void choice_window::populate() { attach(rows_); }

int choice_window::layout() {
    rows_.set_bounds(native::rect(12, 12, get_client_bounds().w() - 24, 240));
    return 252;
}

void choice_window::ask(const std::string &title,
                        std::vector<std::string> labels,
                        std::function<void(std::size_t)> done) {
    done_ = std::move(done);
    set_title(title);
    if (!get_created())
        create();
    rows_.set_items(std::move(labels));
    open();
}

bool choice_window::accept() {
    const int index = rows_.get_selected_index();
    if (index < 0)
        return false;
    if (done_) {
        const auto done = done_;
        const auto chosen = static_cast<std::size_t>(index);
        native::app::post([done, chosen] { done(chosen); });
    }
    return true;
}

} // namespace ui
