//
// The application-wide preferences that are ported: what prefs_common
// keeps, reduced to the settings Blaise acts on.
//
// Derived from Claws Mail src/prefs_common.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef PREFS_COMMON_H
#define PREFS_COMMON_H

#include <filesystem>
#include <string>

namespace prefs {

// Application preferences; defaults match Claws'.
struct common {
    // Receiving.
    bool auto_check = false;
    int check_minutes = 10;
    bool check_on_startup = false;
    // Compose.
    int wrap_length = 72;
    bool auto_wrap = true;
    bool reply_with_quote = true;
    std::string quote_mark = "> ";
    std::string reply_format = "On %d\\n%f wrote:\\n\\n%q";
    std::string forward_format =
        "\\n\\nBegin forwarded message:\\n\\n"
        "Date: %d\\nFrom: %f\\nTo: %t\\nSubject: %s\\n\\n%M";
    bool save_message_to_sent = true;
    // Display.
    bool show_html_as_text = true;
    bool mark_read_on_open = true;
    bool threaded_by_default = true;
    int message_font_size = 11;
    // Trash.
    bool ask_on_delete = false;
    bool empty_trash_on_exit = false;
    // Window.
    int window_width = 1000;
    int window_height = 760;
    int folder_pane_width = 220;
    int summary_pane_height = 280;

    // Read from a configuration directory; a missing file keeps defaults.
    void load(const std::filesystem::path &directory);

    // Write to a configuration directory.
    bool save(const std::filesystem::path &directory) const;
};

} // namespace prefs

#endif
