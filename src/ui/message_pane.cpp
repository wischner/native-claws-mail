//
// The reading pane.
//
// Derived from Claws Mail src/textview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "message_pane.h"

#include "util/text.h"

namespace ui {
namespace {

constexpr int reading_width = 90;

// Return a code_edit style for each text_style, in enum order.
std::vector<native::code_style> message_styles() {
    const auto style = [](std::uint8_t r, std::uint8_t g, std::uint8_t b,
                          bool bold = false) {
        native::code_style item;
        item.foreground = native::rgba(r, g, b, 255);
        item.bold = bold;
        return item;
    };
    return {
        style(0, 0, 0),             // plain
        style(40, 44, 52, true),    // header_name
        style(40, 44, 52),          // header_value
        style(40, 44, 52, true),    // subject
        style(30, 132, 73),         // quote_1
        style(31, 78, 158),         // quote_2
        style(118, 68, 138),        // quote_3
        style(120, 120, 120),       // signature
        style(31, 78, 158),         // link
        style(198, 60, 48),         // attachment
    };
}

} // namespace

message_pane::message_pane() : native::code_edit("", 0, 0, 760, 340) {
    set_show_line_numbers(false);
    set_read_only(true);
    // A message is not source: no gutter color, no current line band.
    native::code_theme theme;
    theme.gutter_background = native::rgba(255, 255, 255, 255);
    theme.current_line = native::rgba(255, 255, 255, 255);
    theme.styles = message_styles();
    set_code_theme(std::move(theme));
    on_mouse_click.connect([this](native::mouse_event event) {
        clicked(event);
        return false;
    });
}

void message_pane::show_message(std::string raw, bool full) {
    raw_ = std::move(raw);
    document_ = view::render_document(raw_, full, reading_width);
    set_text(document_.text);
    apply_style();
    go_to_offset(0);
}

void message_pane::show_note(const std::string &text) {
    raw_.clear();
    document_ = {};
    document_.text = text;
    set_text(text);
    set_style_runs({});
}

void message_pane::clear() { show_note(""); }

void message_pane::apply_style() {
    std::vector<native::style_run> runs;
    runs.reserve(document_.spans.size());
    for (const view::styled_span &span : document_.spans)
        runs.push_back({{span.start, span.end},
                        static_cast<int>(span.style)});
    set_style_runs(std::move(runs));
}

void message_pane::clicked(native::mouse_event event) {
    if (event.action != native::mouse_action::release ||
        event.button != native::mouse_button::left)
        return;
    const std::size_t offset = get_caret_offset();
    if (const view::link_span *link = document_.link_at(offset)) {
        if (on_link)
            on_link(link->target);
    } else if (const auto index = document_.attachment_at(offset)) {
        if (on_attachment)
            on_attachment(*index);
    }
}

bool message_pane::find_next(const std::string &text) {
    if (text.empty())
        return false;
    const std::string &body = get_text();
    std::size_t hit = util::ifind(body, text, get_caret_offset() + 1);
    if (hit == std::string::npos)
        hit = util::ifind(body, text, 0);
    if (hit == std::string::npos)
        return false;
    go_to_offset(hit);
    return true;
}

} // namespace ui
