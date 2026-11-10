//
// The message list.
//
// Derived from Claws Mail src/summaryview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "summary_table.h"

#include "util/date.h"

namespace ui {
std::size_t summary_source::row_count() const {
    return model_.rows().size();
}

native::table_row_id summary_source::row_id(std::size_t row) const {
    return model_.at(row).number;
}

native::table_cell summary_source::cell(std::size_t row,
                                        native::table_column_id column) const {
    const mail::msg_info &info = model_.at(row);
    switch (column) {
    case status:
        return {view::status_text(info), nullptr};
    case subject:
        return {view::subject_text(info, model_.rows()[row].depth), nullptr};
    case from:
        return {info.from_name, nullptr};
    case date:
        return {info.date_time != 0 ? util::format_list_date(info.date_time)
                                    : info.date,
                nullptr};
    case size:
        return {view::size_text(info.size), nullptr};
    default:
        return {};
    }
}

void summary_source::reset() { on_change.emit({}); }

summary_table::summary_table(view::summary_model &model)
    : native::table_view(0, 0, 760, 280), model_(model), source_(model) {
    native::table_column status;
    status.id = summary_source::status;
    status.title = "";
    status.width = 40;
    status.sortable = true;
    native::table_column subject;
    subject.id = summary_source::subject;
    subject.title = "Subject";
    subject.width = 280;
    subject.sortable = true;
    native::table_column from;
    from.id = summary_source::from;
    from.title = "From";
    from.width = 140;
    from.sortable = true;
    native::table_column date;
    date.id = summary_source::date;
    date.title = "Date";
    date.width = 176;
    date.sortable = true;
    native::table_column size;
    size.id = summary_source::size;
    size.title = "Size";
    size.width = 60;
    size.alignment = native::table_alignment::end;
    size.sortable = true;
    set_columns({status, subject, from, date, size});
    set_selection_mode(native::table_selection_mode::multiple);
    set_data_mode(native::table_data_mode::virtualized);
    set_type_search_enabled(false);
    set_model(&source_);
}

summary_table::~summary_table() { set_model(nullptr); }

void summary_table::refresh() {
    const std::vector<native::table_row_id> selected = get_selected_rows();
    source_.reset();
    std::vector<native::table_row_id> kept;
    for (const native::table_row_id id : selected)
        if (model_.row_of(static_cast<std::uint32_t>(id)))
            kept.push_back(id);
    set_selected_rows(kept);
}

std::vector<std::uint32_t> summary_table::selected_numbers() const {
    std::vector<std::uint32_t> numbers;
    for (const native::table_row_id id : get_selected_rows())
        numbers.push_back(static_cast<std::uint32_t>(id));
    return numbers;
}

void summary_table::select_number(std::uint32_t number) {
    if (!model_.row_of(number))
        return;
    set_selected_rows({number});
    ensure_row_visible(number);
}

void summary_table::draw_cell_content(
    native::gpx &graphics, native::theme &appearance,
    native::table_row_id row, std::size_t model_row,
    const native::table_column &column, const native::table_cell &cell,
    const native::rect &bounds, const native::theme::state &state) {
    const bool emphasized =
        (column.id == summary_source::subject ||
         column.id == summary_source::from) &&
        model_row < model_.rows().size() &&
        model_.at(model_row).has(mail::flag_unread);
    // Use Native's stock table font, padding, selection colors and ellipsis.
    native::table_view::draw_cell_content(graphics, appearance, row,
                                          model_row, column, cell, bounds,
                                          state);
    if (emphasized) {
        // Match code_edit's bold field labels: overstrike the same system
        // glyphs one pixel to the right without changing their font metrics.
        native::rect bold_bounds = bounds;
        ++bold_bounds.p.x;
        native::table_view::draw_cell_content(graphics, appearance, row,
                                              model_row, column, cell,
                                              bold_bounds, state);
    }
}

} // namespace ui
