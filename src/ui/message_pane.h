//
// The reading pane. textview.c's GtkTextView with tags becomes a
// read-only native::code_edit given a view::document, which brings
// selection, copy and search with it on every backend.
//
// Derived from Claws Mail src/textview.c and src/messageview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_MESSAGE_PANE_H
#define UI_MESSAGE_PANE_H

#include <functional>
#include <string>

#include <native.h>

#include "view/document.h"

namespace ui {

// Shows one message.
class message_pane final : public native::code_edit {
public:
    // Create an empty pane.
    message_pane();

    //
    // Show a raw message.
    //
    // Parameters:
    //      raw         - The message.
    //      full        - Show every header.
    //
    void show_message(std::string raw, bool full);

    // Show plain text instead, such as the welcome note.
    void show_note(const std::string &text);

    // Clear the pane.
    void clear();

    // Return the raw message on display.
    const std::string &raw() const { return raw_; }

    // Return the laid-out document.
    const view::document &document() const { return document_; }

    // Find text after the caret, wrapping; returns whether it was found.
    bool find_next(const std::string &text);

    // Called when a link is clicked.
    std::function<void(const std::string &)> on_link;

    // Called when an attachment line is clicked.
    std::function<void(std::size_t)> on_attachment;

private:
    void apply_style();
    void clicked(native::mouse_event event);

    std::string raw_;
    view::document document_;
};

} // namespace ui

#endif
