//
// The message editor. compose.c's window with the suite's shell: the same
// strip of tools, the same status bar and native::code_edit for the
// body, which brings undo and clipboard on every backend. All message
// logic is in lib/compose.
//
// Derived from Claws Mail src/compose.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_COMPOSE_WINDOW_H
#define UI_COMPOSE_WINDOW_H

#include <functional>
#include <string>
#include <vector>

#include <native.h>

#include "addr/book.h"
#include "app/engine.h"

class desktop_test;

namespace ui {

// One message being written.
class compose_window final : public native::modeless_wnd {
public:
    //
    // Open a window.
    //
    // Parameters:
    //      owner       - The main window.
    //      core        - Sends and saves.
    //      books       - Address book for completion; must outlive this.
    //      text        - Starting draft.
    //      closed      - Runs after the window has gone.
    //
    compose_window(native::app_wnd &owner, app::engine &core,
                   const addr::book_list &books, compose::draft text,
                   std::function<void(compose_window *)> closed);

    void request_close() override;

    // Return the draft as the fields now hold it.
    compose::draft current() const;

private:
    friend class ::desktop_test;

    void build_toolbar();
    void place_children();
    void run(std::string_view command);
    void send();
    void save();
    void attach_file();
    void update_title();
    void update_attachments();
    void update_completion(native::combo_box &field,
                           const std::string &text);
    bool has_recipients() const;
    void finish();

    app::engine &core_;
    const addr::book_list &books_;
    prefs::account_list accounts_;
    compose::draft draft_;
    std::function<void(compose_window *)> closed_;
    native::combo_box from_;
    native::combo_box to_;
    native::combo_box cc_;
    native::text_edit bcc_;
    native::text_edit subject_;
    native::code_edit body_;
    native::list files_;
    native::toolbar toolbar_;
    native::status_bar status_;
    native::open_file_dialog attach_dialog_;
    bool dirty_ = false;
    bool updating_ = false;
};

} // namespace ui

#endif
