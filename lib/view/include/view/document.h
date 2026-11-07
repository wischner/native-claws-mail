//
// A message laid out for reading: text plus what each part of it is.
// textview.c writes straight into a GtkTextBuffer with tags; here it
// produces a value, and the message pane maps it onto native::code_edit.
//
// Derived from Claws Mail src/textview.c and src/messageview.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef VIEW_DOCUMENT_H
#define VIEW_DOCUMENT_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace view {

// What a span of the text is.
enum class text_style {
    plain, header_name, header_value, subject, quote_1, quote_2, quote_3,
    signature, link, attachment
};

// One styled byte range.
struct styled_span {
    std::size_t start = 0;
    std::size_t end = 0;
    text_style style = text_style::plain;
};

// One link in the text.
struct link_span {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string target;
};

// One attachment listed at the end of the text.
struct attachment_entry {
    std::string name;
    std::string mime_type;
    std::size_t size = 0;
    std::size_t line_start = 0;
    std::size_t line_end = 0;
};

// A message prepared for display.
struct document {
    std::string text;
    std::vector<styled_span> spans;     // Sorted, not overlapping.
    std::vector<link_span> links;
    std::vector<attachment_entry> attachments;

    // Return the link at an offset, or null.
    const link_span *link_at(std::size_t offset) const;

    // Return the attachment index at an offset, or nothing.
    std::optional<std::size_t> attachment_at(std::size_t offset) const;
};

//
// Lay out a raw message.
//
// Parameters:
//      raw         - Complete message.
//      full_headers - Show every header rather than the usual few.
//      wrap_width  - Wrap body lines to this many columns; 0 keeps them.
//
document render_document(std::string_view raw, bool full_headers = false,
                         int wrap_width = 0);

// Return the decoded bytes of attachment index in a raw message.
std::string attachment_data(std::string_view raw, std::size_t index);

// Return how deeply a line is quoted.
int quote_depth(std::string_view line);

} // namespace view

#endif
