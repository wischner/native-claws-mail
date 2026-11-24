//
// Shared parts of the dialogs. Native has no label control, so a form
// keeps its captions as a list and paints them, as Writer and Isaac do
// for their prompts; every dialog here is a form with OK and Cancel.
//
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_FORM_WINDOW_H
#define UI_FORM_WINDOW_H

#include <functional>
#include <string>
#include <vector>

#include <native.h>

class desktop_test;

namespace ui {

inline constexpr int dialog_button_width = 96;
inline constexpr int dialog_gap = 8;
inline constexpr int dialog_margin = 12;

// Native edit-form metrics, resolved after the window is created.
int form_control_height(native::wnd &window);
void tab_sequence(std::initializer_list<native::wnd *> controls);

// A modal dialog with captions and an OK and Cancel pair.
class form_window : public native::modal_wnd {
public:
    //
    // Create a dialog; it stays hidden until open() is called.
    //
    // Parameters:
    //      owner       - The window it blocks.
    //      title       - Window title.
    //      width, height - Client size.
    //
    form_window(native::app_wnd &owner, const std::string &title, int width,
                int height);

    // Show the dialog.
    void open();

protected:
    // Add a caption at a position, painted in the dialog's color.
    void add_caption(const std::string &text, int x, int y);

    // Replace a caption's text.
    void set_caption(std::size_t index, const std::string &text);

    // Parent, create and show one child; call from populate().
    void attach(native::wnd &child);

    // Create the children, once, on first open.
    virtual void populate() = 0;

    // Lay out content using the current Native form metrics.
    virtual int layout() = 0;
    int field_height() { return form_control_height(*this); }
    int row_y(int row) { return 12 + row * (field_height() + 8); }
    void caption_bounds(std::size_t index, int x, int y, int width);

    // Refresh the children just before showing.
    virtual void prepare() {}

    // Return false to keep the dialog open after OK.
    virtual bool accept() { return true; }

    native::button ok_;
    native::button cancel_;

private:
    friend class ::desktop_test;
    // One painted caption.
    struct caption {
        std::string text;
        int x;
        int y;
        int width = 400;
    };

    void finish(bool accepted);

    std::vector<caption> captions_;
    void place_children();
    std::vector<native::wnd *> controls_;
    bool populated_ = false;
    bool laying_out_ = false;
    int minimum_width_;
};

// Asks for one line of text.
class input_window final : public form_window {
public:
    // Create the dialog.
    explicit input_window(native::app_wnd &owner);

    //
    // Ask a question.
    //
    // Parameters:
    //      title   - The window title.
    //      prompt  - The question.
    //      initial - The starting answer.
    //      done    - Runs with the answer unless the user cancels.
    //
    void ask(const std::string &title, const std::string &prompt,
             const std::string &initial,
             std::function<void(const std::string &)> done);

protected:
    void populate() override;
    int layout() override;
    bool accept() override;

private:
    friend class ::desktop_test;
    std::function<void(const std::string &)> done_;
    native::text_edit answer_;
};

// Picks one string from a list, such as a destination folder.
class choice_window final : public form_window {
public:
    // Create the dialog.
    explicit choice_window(native::app_wnd &owner);

    //
    // Offer a choice.
    //
    // Parameters:
    //      title   - The window title.
    //      labels  - Rows to show.
    //      done    - Runs with the chosen index.
    //
    void ask(const std::string &title, std::vector<std::string> labels,
             std::function<void(std::size_t)> done);

protected:
    void populate() override;
    int layout() override;
    bool accept() override;

private:
    friend class ::desktop_test;
    std::function<void(std::size_t)> done_;
    native::list rows_;
};

} // namespace ui

#endif
