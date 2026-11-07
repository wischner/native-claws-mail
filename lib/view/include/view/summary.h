//
// The message list as data. summaryview.c binds sorting, threading and
// selection to a GTK tree; here they are a model that the window reads,
// so all of it is tested without a display.
//
// Derived from Claws Mail src/summaryview.c and src/procmsg.c
// (procmsg_get_thread_tree),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef VIEW_SUMMARY_H
#define VIEW_SUMMARY_H

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "filter/matcher.h"
#include "mail/folder.h"

namespace view {

// A summary row.
struct summary_row {
    std::size_t message = 0;    // Index into messages().
    int depth = 0;              // Thread nesting.
    bool has_children = false;
};

// The list shown for one folder.
class summary_model {
public:
    // Replace the messages, keeping sort and filter settings.
    void set_messages(std::vector<mail::msg_info> messages);

    // Return the stored messages.
    const std::vector<mail::msg_info> &messages() const { return messages_; }

    // Change the sort; rebuilds rows.
    void set_sort(mail::sort_key key, bool ascending);

    // Return the sort key.
    mail::sort_key sort() const { return sort_; }

    // Return whether the sort ascends.
    bool sort_ascending() const { return ascending_; }

    // Turn threading on or off.
    void set_threaded(bool threaded);

    // Return whether rows are threaded.
    bool threaded() const { return threaded_; }

    // Show only unread mail.
    void set_hide_read(bool hide);

    // Return whether read mail is hidden.
    bool hide_read() const { return hide_read_; }

    //
    // Set the quick search.
    //
    // Parameters:
    //      conditions  - Filter, or nothing to show everything.
    //      raw         - Fetches message text for body searches.
    //
    void set_filter(std::optional<filter::condition_list> conditions,
                    std::function<std::optional<std::string>(std::uint32_t)>
                        raw = {});

    // Show only these messages; nothing shows everything. A search that
    // reads message text runs elsewhere and hands its result to this.
    void set_matches(std::optional<std::set<std::uint32_t>> numbers);

    // Return the rows in display order.
    const std::vector<summary_row> &rows() const { return rows_; }

    // Return the message of a row.
    const mail::msg_info &at(std::size_t row) const;

    // Return the row of a message number, or nothing.
    std::optional<std::size_t> row_of(std::uint32_t number) const;

    // Change a message's flags; returns its row if it is shown.
    std::optional<std::size_t> set_flags(std::uint32_t number,
                                         std::uint32_t perm_flags);

    // Return the first unread row after a row, or nothing.
    std::optional<std::size_t> next_unread(std::size_t after) const;

    // Return the first unread row, or nothing.
    std::optional<std::size_t> first_unread() const;

    // Return the last unread row before a row, or nothing.
    std::optional<std::size_t> previous_unread(std::size_t before) const;

private:
    void rebuild();
    bool visible(const mail::msg_info &info) const;
    bool sort_before(const mail::msg_info &left,
                     const mail::msg_info &right) const;

    std::vector<mail::msg_info> messages_;
    std::vector<summary_row> rows_;
    mail::sort_key sort_ = mail::sort_key::date;
    bool ascending_ = true;
    bool threaded_ = false;
    bool hide_read_ = false;
    std::optional<filter::condition_list> filter_;
    std::optional<std::set<std::uint32_t>> matches_;
    std::function<std::optional<std::string>(std::uint32_t)> raw_;
};

// The short status text of a row: "N", "U", "!" and so on.
std::string status_text(const mail::msg_info &info);

// The size of a message for display.
std::string size_text(std::uint64_t size);

// The subject of a row, with thread indent.
std::string subject_text(const mail::msg_info &info, int depth);

} // namespace view

#endif
