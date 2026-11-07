//
// Sorting, threading and filtering the message list.
//
// Derived from Claws Mail src/summaryview.c and src/procmsg.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "view/summary.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

#include "util/text.h"

namespace view {
namespace {

// Compare two strings ignoring ASCII case.
int compare_text(std::string_view left, std::string_view right) {
    const std::string a = util::lower(left);
    const std::string b = util::lower(right);
    return a.compare(b);
}

// Return a message's parent index in a thread, or nothing.
std::optional<std::size_t> find_parent(
    const mail::msg_info &info,
    const std::unordered_map<std::string, std::size_t> &by_id) {
    if (!info.in_reply_to.empty())
        if (const auto it = by_id.find(info.in_reply_to); it != by_id.end())
            return it->second;
    for (auto reference = info.references.rbegin();
         reference != info.references.rend(); ++reference)
        if (const auto it = by_id.find(*reference); it != by_id.end())
            return it->second;
    return std::nullopt;
}

} // namespace

void summary_model::set_messages(std::vector<mail::msg_info> messages) {
    messages_ = std::move(messages);
    rebuild();
}

void summary_model::set_sort(mail::sort_key key, bool ascending) {
    sort_ = key;
    ascending_ = ascending;
    rebuild();
}

void summary_model::set_threaded(bool threaded) {
    threaded_ = threaded;
    rebuild();
}

void summary_model::set_hide_read(bool hide) {
    hide_read_ = hide;
    rebuild();
}

void summary_model::set_filter(
    std::optional<filter::condition_list> conditions,
    std::function<std::optional<std::string>(std::uint32_t)> raw) {
    filter_ = std::move(conditions);
    raw_ = std::move(raw);
    rebuild();
}

void summary_model::set_matches(
    std::optional<std::set<std::uint32_t>> numbers) {
    matches_ = std::move(numbers);
    rebuild();
}

bool summary_model::visible(const mail::msg_info &info) const {
    if (hide_read_ && !info.has(mail::flag_unread))
        return false;
    if (matches_ && !matches_->contains(info.number))
        return false;
    if (!filter_)
        return true;
    const std::uint32_t number = info.number;
    return filter_->matches(
        {info,
         raw_ ? std::function<std::optional<std::string>()>(
                    [this, number] { return raw_(number); })
              : std::function<std::optional<std::string>()>(),
         0});
}

bool summary_model::sort_before(const mail::msg_info &left,
                                const mail::msg_info &right) const {
    int order = 0;
    switch (sort_) {
    case mail::sort_key::number:
    case mail::sort_key::none:
        order = left.number < right.number ? -1
                                           : left.number > right.number;
        break;
    case mail::sort_key::size:
        order = left.size < right.size ? -1 : left.size > right.size;
        break;
    case mail::sort_key::from:
        order = compare_text(left.from_name, right.from_name);
        break;
    case mail::sort_key::to:
        order = compare_text(left.to, right.to);
        break;
    case mail::sort_key::subject:
        order = compare_text(mail::strip_subject(left.subject),
                             mail::strip_subject(right.subject));
        break;
    case mail::sort_key::unread:
        order = static_cast<int>(right.has(mail::flag_unread)) -
                static_cast<int>(left.has(mail::flag_unread));
        break;
    case mail::sort_key::mark:
        order = static_cast<int>(right.has(mail::flag_marked)) -
                static_cast<int>(left.has(mail::flag_marked));
        break;
    case mail::sort_key::label:
        order = left.color_label() - right.color_label();
        break;
    default:
        order = left.date_time < right.date_time
                    ? -1
                    : left.date_time > right.date_time;
        break;
    }
    // Equal keys fall back to number, so the order is total and stable.
    if (order == 0)
        order = left.number < right.number ? -1
                                           : left.number > right.number;
    return ascending_ ? order < 0 : order > 0;
}

void summary_model::rebuild() {
    rows_.clear();
    std::vector<std::size_t> shown;
    for (std::size_t i = 0; i < messages_.size(); ++i)
        if (visible(messages_[i]))
            shown.push_back(i);
    const auto before = [this](std::size_t a, std::size_t b) {
        return sort_before(messages_[a], messages_[b]);
    };
    if (!threaded_) {
        std::ranges::sort(shown, before);
        for (const std::size_t index : shown)
            rows_.push_back({index, 0, false});
        return;
    }
    // Link each message to its parent among those shown.
    std::unordered_map<std::string, std::size_t> by_id;
    for (const std::size_t index : shown)
        if (!messages_[index].message_id.empty())
            by_id.emplace(messages_[index].message_id, index);
    // Headers can claim a cycle. Each walk upward colors what it passes,
    // so reaching this walk's own color means the link just taken closes
    // a loop and is dropped; every node is walked once.
    std::unordered_map<std::size_t, std::size_t> parents;
    for (const std::size_t index : shown)
        if (const auto parent = find_parent(messages_[index], by_id);
            parent && *parent != index)
            parents[index] = *parent;
    std::unordered_map<std::size_t, std::size_t> walked;
    for (const std::size_t index : shown) {
        std::size_t current = index;
        while (!walked.contains(current)) {
            walked[current] = index;
            const auto up = parents.find(current);
            if (up == parents.end())
                break;
            const auto seen = walked.find(up->second);
            if (seen != walked.end() && seen->second == index) {
                parents.erase(up);
                break;
            }
            current = up->second;
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> children;
    std::vector<std::size_t> roots;
    for (const std::size_t index : shown) {
        if (const auto up = parents.find(index); up != parents.end())
            children[up->second].push_back(index);
        else
            roots.push_back(index);
    }
    std::ranges::sort(roots, before);
    // Depth first with an explicit stack: a thread can be very long.
    struct frame {
        std::size_t index;
        int depth;
    };
    std::vector<frame> stack;
    for (auto root = roots.rbegin(); root != roots.rend(); ++root)
        stack.push_back({*root, 0});
    while (!stack.empty()) {
        const frame top = stack.back();
        stack.pop_back();
        auto found = children.find(top.index);
        const bool parent = found != children.end();
        rows_.push_back({top.index, top.depth, parent});
        if (!parent)
            continue;
        auto &replies = found->second;
        std::ranges::sort(replies, [this](std::size_t a, std::size_t b) {
            return messages_[a].date_time != messages_[b].date_time
                       ? messages_[a].date_time < messages_[b].date_time
                       : a < b;
        });
        for (auto reply = replies.rbegin(); reply != replies.rend(); ++reply)
            stack.push_back({*reply, top.depth + 1});
    }
}

const mail::msg_info &summary_model::at(std::size_t row) const {
    return messages_[rows_[row].message];
}

std::optional<std::size_t>
summary_model::row_of(std::uint32_t number) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (at(i).number == number)
            return i;
    return std::nullopt;
}

std::optional<std::size_t>
summary_model::set_flags(std::uint32_t number, std::uint32_t perm_flags) {
    for (mail::msg_info &info : messages_)
        if (info.number == number)
            info.perm_flags = perm_flags;
    return row_of(number);
}

std::optional<std::size_t> summary_model::first_unread() const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (at(i).has(mail::flag_unread))
            return i;
    return std::nullopt;
}

std::optional<std::size_t>
summary_model::next_unread(std::size_t after) const {
    for (std::size_t i = after + 1; i < rows_.size(); ++i)
        if (at(i).has(mail::flag_unread))
            return i;
    return std::nullopt;
}

std::optional<std::size_t>
summary_model::previous_unread(std::size_t before) const {
    for (std::size_t i = std::min(before, rows_.size()); i > 0; --i)
        if (at(i - 1).has(mail::flag_unread))
            return i - 1;
    return std::nullopt;
}

std::string status_text(const mail::msg_info &info) {
    std::string out;
    if (info.has(mail::flag_new))
        out += 'N';
    else if (info.has(mail::flag_unread))
        out += 'U';
    if (info.has(mail::flag_replied))
        out += 'R';
    if (info.has(mail::flag_forwarded))
        out += 'F';
    if (info.has(mail::flag_marked))
        out += '!';
    if (info.has(mail::flag_locked))
        out += 'L';
    if ((info.temp_flags & mail::temp_has_attachment) != 0)
        out += '@';
    return out;
}

std::string size_text(std::uint64_t size) {
    char buffer[32];
    if (size < 1024)
        std::snprintf(buffer, sizeof buffer, "%lluB",
                      static_cast<unsigned long long>(size));
    else if (size < 1024 * 1024)
        std::snprintf(buffer, sizeof buffer, "%.1fKB",
                      static_cast<double>(size) / 1024);
    else
        std::snprintf(buffer, sizeof buffer, "%.1fMB",
                      static_cast<double>(size) / (1024 * 1024));
    return buffer;
}

std::string subject_text(const mail::msg_info &info, int depth) {
    std::string out(static_cast<std::size_t>(std::min(depth, 16)) * 2, ' ');
    out += info.subject.empty() ? "(No Subject)" : info.subject;
    return out;
}

} // namespace view
