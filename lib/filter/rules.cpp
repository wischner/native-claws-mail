//
// Filtering rules.
//
// Derived from Claws Mail src/filtering.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "filter/rules.h"

#include <array>

#include "util/files.h"
#include "util/log.h"
#include "util/text.h"

namespace filter {
namespace {

// A simple action keyword.
struct action_word {
    const char *name;
    action_kind kind;
};

constexpr std::array<action_word, 11> plain_actions{{
    {"delete", action_kind::remove},
    {"mark", action_kind::mark},
    {"unmark", action_kind::unmark},
    {"lock", action_kind::lock},
    {"unlock", action_kind::unlock},
    {"mark_as_read", action_kind::mark_read},
    {"mark_as_unread", action_kind::mark_unread},
    {"mark_as_spam", action_kind::mark_spam},
    {"mark_as_ham", action_kind::mark_ham},
    {"ignore", action_kind::ignore},
    {"watch", action_kind::watch},
}};

// Return a string quoted for matcherrc.
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// Parse every action on the rest of a line.
std::vector<action> parse_actions(tokenizer &tokens) {
    std::vector<action> actions;
    while (!tokens.at_end()) {
        std::string word = tokens.word();
        action step;
        if (word == "move" || word == "copy") {
            step.kind = word == "move" ? action_kind::move : action_kind::copy;
            step.destination = tokens.quoted().value_or("");
        } else if (word == "color") {
            step.kind = action_kind::color;
            step.value = tokens.number().value_or(0);
        } else if (word == "stop") {
            step.kind = action_kind::stop;
        } else {
            step.kind = action_kind::unsupported;
            step.text = word;
            for (const action_word &entry : plain_actions)
                if (word == entry.name)
                    step.kind = entry.kind;
            // Keep an unsupported action's arguments for round trips.
            while (step.kind == action_kind::unsupported && !tokens.at_end()) {
                const std::string next = tokens.peek();
                if (!next.empty() && (next[0] >= 'a' && next[0] <= 'z') &&
                    next.find(' ') == std::string::npos &&
                    !util::parse_int(next))
                    break;
                step.text += " " + quote(tokens.word());
            }
        }
        actions.push_back(std::move(step));
    }
    return actions;
}

// Run the side effects of one action. Returns false to stop the rule list.
bool run_action(const action &step, mail::folder_item &item,
                std::uint32_t number, const mail::folder_list &folders,
                bool &left_folder) {
    const auto flag_step = [&](std::uint32_t set, std::uint32_t clear) {
        item.change_flags(number, set, clear);
        return true;
    };
    switch (step.kind) {
    case action_kind::move:
    case action_kind::copy: {
        mail::folder_item *target = folders.find(step.destination);
        if (target == nullptr) {
            util::log_write(util::log_level::warning,
                            "Filter destination not found: " +
                                step.destination);
            return true;
        }
        const bool keep = step.kind == action_kind::copy;
        if (&item == target)
            return true;
        if (mail::transfer_messages(item, *target, {number}, keep) == 1 &&
            !keep) {
            left_folder = true;
            return false;
        }
        return true;
    }
    case action_kind::remove:
        if (item.owner().remove(item, number)) {
            item.forget(number);
            left_folder = true;
        }
        return false;
    case action_kind::mark:
        return flag_step(mail::flag_marked, 0);
    case action_kind::unmark:
        return flag_step(0, mail::flag_marked);
    case action_kind::lock:
        return flag_step(mail::flag_locked, 0);
    case action_kind::unlock:
        return flag_step(0, mail::flag_locked);
    case action_kind::mark_read:
        return flag_step(0, mail::flag_new | mail::flag_unread);
    case action_kind::mark_unread:
        return flag_step(mail::flag_unread, 0);
    case action_kind::mark_spam:
        return flag_step(mail::flag_spam, 0);
    case action_kind::mark_ham:
        return flag_step(0, mail::flag_spam);
    case action_kind::ignore:
        return flag_step(mail::flag_ignore_thread, 0);
    case action_kind::watch:
        return flag_step(mail::flag_watch_thread, 0);
    case action_kind::color: {
        mail::msg_info labelled;
        labelled.set_color_label(step.value);
        return flag_step(labelled.perm_flags, mail::flag_clabel_mask);
    }
    case action_kind::stop:
        return false;
    case action_kind::unsupported:
        util::log_write(util::log_level::warning,
                        "Filter action not supported: " + step.text);
        return true;
    }
    return true;
}

} // namespace

std::string action::to_string() const {
    switch (kind) {
    case action_kind::move:
        return "move " + quote(destination);
    case action_kind::copy:
        return "copy " + quote(destination);
    case action_kind::color:
        return "color " + std::to_string(value);
    case action_kind::stop:
        return "stop";
    case action_kind::unsupported:
        return text;
    default:
        break;
    }
    for (const action_word &entry : plain_actions)
        if (entry.kind == kind)
            return entry.name;
    return {};
}

std::string rule::to_string() const {
    std::string out = enabled ? "enabled" : "disabled";
    out += " rulename " + quote(name);
    if (account_id != 0)
        out += " account " + std::to_string(account_id);
    out += " " + conditions.to_string();
    for (const action &step : actions)
        out += " " + step.to_string();
    return out;
}

std::optional<rule> parse_rule(std::string_view line) {
    tokenizer tokens(line);
    rule parsed;
    if (tokens.peek() == "enabled" || tokens.peek() == "disabled")
        parsed.enabled = tokens.word() == "enabled";
    if (tokens.peek() == "rulename") {
        tokens.word();
        parsed.name = tokens.quoted().value_or("");
    }
    if (tokens.peek() == "account") {
        tokens.word();
        parsed.account_id = tokens.number().value_or(0);
    }
    auto conditions = parse_condition_list(tokens);
    if (!conditions)
        return std::nullopt;
    parsed.conditions = std::move(*conditions);
    parsed.actions = parse_actions(tokens);
    if (parsed.actions.empty())
        return std::nullopt;
    return parsed;
}

void rule_set::load(const std::filesystem::path &path) {
    rules_.clear();
    other_sections_.clear();
    const auto text = util::read_file(path);
    if (!text)
        return;
    bool in_filtering = false;
    for (const std::string &line : util::split_lines(*text)) {
        if (line.starts_with('[') && line.ends_with(']')) {
            in_filtering = line == "[filtering]";
            if (in_filtering)
                continue;
        }
        if (!in_filtering) {
            other_sections_ += line + "\n";
            continue;
        }
        if (util::trim(line).empty() || line.starts_with('#'))
            continue;
        if (auto parsed = parse_rule(line))
            rules_.push_back(std::move(*parsed));
        else
            util::log_write(util::log_level::warning,
                            "Filter rule not understood: " + line);
    }
}

bool rule_set::save(const std::filesystem::path &path) const {
    std::string out = other_sections_;
    out += "[filtering]\n";
    for (const rule &item : rules_)
        out += item.to_string() + "\n";
    return util::write_file(path, out);
}

int apply_rules(const std::vector<rule> &rules, mail::folder_item &item,
                const std::vector<std::uint32_t> &numbers,
                const mail::folder_list &folders, int account_id) {
    int left = 0;
    for (const std::uint32_t number : numbers) {
        bool left_folder = false;
        bool running = true;
        for (const rule &current : rules) {
            if (!running)
                break;
            if (!current.enabled ||
                (current.account_id != 0 && account_id != 0 &&
                 current.account_id != account_id))
                continue;
            const mail::msg_info *info = item.find(number);
            if (info == nullptr)
                break;
            const mail::msg_info snapshot = *info;
            const match_input input{
                snapshot,
                [&] { return item.owner().fetch(item, number); },
                0};
            if (!current.conditions.matches(input))
                continue;
            for (const action &step : current.actions)
                if (!run_action(step, item, number, folders, left_folder)) {
                    running = false;
                    break;
                }
        }
        left += left_folder ? 1 : 0;
    }
    item.save();
    return left;
}

} // namespace filter
