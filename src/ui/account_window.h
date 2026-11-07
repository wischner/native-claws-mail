//
// Account settings. prefs_account.c's nine pages reduce to one form,
// and only what is ported appears on it. A form keeps nothing of its
// own: rows go from the edited copy to the fields and back when another
// account is chosen, and OK hands the whole copy to the engine.
//
// Derived from Claws Mail src/prefs_account.c and src/account.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_ACCOUNT_WINDOW_H
#define UI_ACCOUNT_WINDOW_H

#include <functional>

#include "form_window.h"
#include "prefs/account.h"

class desktop_test;

namespace ui {

// Edits the list of accounts.
class account_window final : public form_window {
public:
    // Create the dialog; done runs with the edited list after OK.
    account_window(native::app_wnd &owner,
                   std::function<void(prefs::account_list)> done);

    // Show the dialog over a copy of the accounts.
    void edit(prefs::account_list accounts);

protected:
    void populate() override;
    void prepare() override;
    bool accept() override;

private:
    friend class ::desktop_test;

    void show_rows();
    void select(int index);
    void load_fields();
    void store_fields();
    void add_account();
    void remove_account();
    void make_default();

    std::function<void(prefs::account_list)> done_;
    prefs::account_list accounts_;
    int current_ = -1;
    bool updating_ = false;
    native::list rows_;
    native::button add_;
    native::button remove_;
    native::button default_;
    native::text_edit name_;
    native::text_edit full_name_;
    native::text_edit address_;
    native::combo_box protocol_;
    native::text_edit server_;
    native::text_edit port_;
    native::combo_box security_;
    native::text_edit user_;
    native::password_edit password_;
    native::text_edit smtp_server_;
    native::text_edit smtp_port_;
    native::combo_box smtp_security_;
    native::check smtp_auth_;
    native::check remove_mail_;
    native::text_edit signature_;
};

} // namespace ui

#endif
