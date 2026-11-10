//
// The message list. summaryview.c's tree becomes native::table_view
// reading view::summary_model through a table_model, so a folder of any
// size stays virtual.
//
// Derived from Claws Mail src/summaryview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_SUMMARY_TABLE_H
#define UI_SUMMARY_TABLE_H

#include <cstdint>
#include <vector>

#include <native.h>

#include "view/summary.h"

namespace ui {

// Adapts the list model to Native.
class summary_source final : public native::table_model {
public:
    // Column ids.
    enum column : native::table_column_id {
        status = 1, subject, from, date, size
    };

    // Wrap a borrowed model.
    explicit summary_source(view::summary_model &model) : model_(model) {}

    std::size_t row_count() const override;
    native::table_row_id row_id(std::size_t row) const override;
    native::table_cell cell(std::size_t row,
                            native::table_column_id column) const override;

    // Tell the view that everything changed.
    void reset();

private:
    view::summary_model &model_;
};

// The table, with unread mail in bold.
class summary_table final : public native::table_view {
public:
    // Create the table over a borrowed model.
    explicit summary_table(view::summary_model &model);

    // Detach the model, which dies before the base class does.
    ~summary_table() override;

    // Reread the model, keeping the selection where it still exists.
    void refresh();

    // Return the selected message numbers.
    std::vector<std::uint32_t> selected_numbers() const;

    // Select one message and scroll to it.
    void select_number(std::uint32_t number);

protected:
    void draw_cell_content(native::gpx &graphics, native::theme &appearance,
                           native::table_row_id row, std::size_t model_row,
                           const native::table_column &column,
                           const native::table_cell &cell,
                           const native::rect &bounds,
                           const native::theme::state &state) override;

private:
    view::summary_model &model_;
    summary_source source_;
};

} // namespace ui

#endif
