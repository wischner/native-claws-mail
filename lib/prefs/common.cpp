//
// Application preferences.
//
// Derived from Claws Mail src/prefs_common.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "prefs/common.h"

#include "prefs/rc_file.h"

namespace prefs {
namespace {

constexpr const char *rc_name = "blaiserc";
constexpr const char *section_name = "Common";

// Move one field between a section and a value: the single place a key
// and its field are paired, used for both directions.
template <typename Visit>
void visit_fields(common &values, Visit &&visit) {
    visit("autochk_newmail", values.auto_check);
    visit("autochk_interval", values.check_minutes);
    visit("check_on_startup", values.check_on_startup);
    visit("linewrap_length", values.wrap_length);
    visit("auto_wrap", values.auto_wrap);
    visit("reply_with_quote", values.reply_with_quote);
    visit("reply_quote_mark", values.quote_mark);
    visit("reply_quote_format", values.reply_format);
    visit("forward_quote_format", values.forward_format);
    visit("save_message", values.save_message_to_sent);
    visit("show_html_as_text", values.show_html_as_text);
    visit("mark_as_read_on_open", values.mark_read_on_open);
    visit("threaded_by_default", values.threaded_by_default);
    visit("message_font_size", values.message_font_size);
    visit("ask_on_delete", values.ask_on_delete);
    visit("empty_trash_on_exit", values.empty_trash_on_exit);
    visit("window_width", values.window_width);
    visit("window_height", values.window_height);
    visit("folder_pane_width", values.folder_pane_width);
    visit("summary_pane_height", values.summary_pane_height);
}

// Read one field; the type of the field chooses the conversion.
struct field_reader {
    const rc_section &from;

    void operator()(const char *key, bool &value) const {
        value = from.get_bool(key, value);
    }
    void operator()(const char *key, int &value) const {
        value = from.get_int(key, value);
    }
    void operator()(const char *key, std::string &value) const {
        value = from.get(key, value);
    }
};

// Write one field.
struct field_writer {
    rc_section &to;

    void operator()(const char *key, bool value) const {
        to.set_bool(key, value);
    }
    void operator()(const char *key, int value) const {
        to.set_int(key, value);
    }
    void operator()(const char *key, const std::string &value) const {
        to.set(key, value);
    }
};

} // namespace

void common::load(const std::filesystem::path &directory) {
    const rc_file file = rc_file::load(directory / rc_name);
    if (const rc_section *block = file.find(section_name))
        visit_fields(*this, field_reader{*block});
}

bool common::save(const std::filesystem::path &directory) const {
    rc_file file = rc_file::load(directory / rc_name);
    common copy = *this;
    visit_fields(copy, field_writer{file.section(section_name)});
    return file.save(directory / rc_name);
}

} // namespace prefs
