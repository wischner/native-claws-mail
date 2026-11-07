//
// The smaller settings dialogs: preferences, filter rules and the
// address book, and the Network Log. prefs_common.c, prefs_filtering.c
// and addressbook.c each hold pages of GTK; these hold the settings
// that are ported.
//
// Derived from Claws Mail src/prefs_common.c, src/prefs_filtering.c,
// src/addressbook.c and src/gtk/logwindow.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_SETTINGS_WINDOWS_H
#define UI_SETTINGS_WINDOWS_H

#include <functional>
#include <vector>

#include "addr/book.h"
#include "filter/rules.h"
#include "form_window.h"
#include "prefs/common.h"

class desktop_test;

namespace ui {

// Edits application preferences.
class preferences_window final : public form_window {
public:
    // Create the dialog; done runs with the new values after OK.
    preferences_window(native::app_wnd &owner,
                       std::function<void(prefs::common)> done);

    // Show the dialog.
    void edit(prefs::common values);

protected:
    void populate() override;
    void prepare() override;
    bool accept() override;

private:
    friend class ::desktop_test;

    std::function<void(prefs::common)> done_;
    prefs::common values_;
    native::check auto_check_;
    native::text_edit minutes_;
    native::check startup_check_;
    native::check auto_wrap_;
    native::text_edit wrap_;
    native::text_edit quote_mark_;
    native::check save_sent_;
    native::check mark_read_;
    native::check threaded_;
    native::check empty_trash_;
};

// Edits filter rules as text, in matcherrc syntax.
class filter_window final : public form_window {
public:
    // Create the dialog; done runs with the new rules after OK.
    filter_window(native::app_wnd &owner,
                  std::function<void(std::vector<filter::rule>)> done);

    // Show the dialog.
    void edit(std::vector<filter::rule> rules);

protected:
    void populate() override;
    void prepare() override;
    bool accept() override;

private:
    friend class ::desktop_test;

    void show_rows();
    void add_rule();
    void replace_rule();
    void remove_rule();
    void move_rule(int by);

    std::function<void(std::vector<filter::rule>)> done_;
    std::vector<filter::rule> rules_;
    native::list rows_;
    native::text_edit text_;
    native::button add_;
    native::button replace_;
    native::button remove_;
    native::button up_;
    native::button down_;
};

// Shows and edits contacts.
class address_window final : public native::modeless_wnd {
public:
    //
    // Create the window.
    //
    // Parameters:
    //      owner       - The main window.
    //      books       - The books; must outlive this.
    //      changed     - Runs after an edit, to save.
    //
    address_window(native::app_wnd &owner, addr::book_list &books,
                   std::function<void()> changed);

    // Show the window.
    void open();

private:
    friend class ::desktop_test;

    void show_rows();
    void show_person();
    void add_person();
    void store_person();
    void remove_person();
    void import_file();
    void place_children();

    addr::book_list &books_;
    std::function<void()> changed_;
    native::list rows_;
    native::text_edit name_;
    native::text_edit email_;
    native::button add_;
    native::button save_;
    native::button remove_;
    native::button import_;
    native::open_file_dialog import_dialog_;
    bool built_ = false;
};

// Shows the session log.
class log_window final : public native::modeless_wnd {
public:
    // Create the window.
    explicit log_window(native::app_wnd &owner);

    // Show the current log.
    void open();

private:
    native::code_edit text_;
    native::toolbar bar_;
};

} // namespace ui

#endif
