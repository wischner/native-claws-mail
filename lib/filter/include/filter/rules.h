//
// Filtering rules: conditions plus actions, in the matcherrc format, and
// running them over messages. Rules Blaise cannot perform, such as
// forward and execute, load and save intact but do nothing yet, so
// sharing a matcherrc with Claws is safe.
//
// Derived from Claws Mail src/filtering.c and src/matcher.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef FILTER_RULES_H
#define FILTER_RULES_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "filter/matcher.h"
#include "mail/folder_list.h"

namespace filter {

// What a rule does.
enum class action_kind {
    move, copy, remove, mark, unmark, lock, unlock, mark_read, mark_unread,
    mark_spam, mark_ham, color, ignore, watch, stop, unsupported
};

// One step of a rule.
struct action {
    action_kind kind = action_kind::stop;
    std::string destination;    // For move and copy.
    int value = 0;              // For color.
    std::string text;           // The raw words of an unsupported action.

    // Return the action in matcherrc syntax.
    std::string to_string() const;
};

// One rule.
struct rule {
    bool enabled = true;
    std::string name;
    int account_id = 0;         // Zero means every account.
    condition_list conditions;
    std::vector<action> actions;

    // Return the rule in matcherrc syntax.
    std::string to_string() const;
};

// Parse one matcherrc line; returns nothing if it is not a rule.
std::optional<rule> parse_rule(std::string_view line);

// A matcherrc file. Sections hold rule text; rules run on [filtering].
class rule_set {
public:
    // Load a file; absent reads as empty.
    void load(const std::filesystem::path &path);

    // Save, keeping the sections Blaise does not use.
    bool save(const std::filesystem::path &path) const;

    // Return the rules applied to arriving mail.
    std::vector<rule> &rules() { return rules_; }
    const std::vector<rule> &rules() const { return rules_; }

private:
    std::string other_sections_;
    std::vector<rule> rules_;
};

//
// Apply rules to messages.
//
// Parameters:
//      rules       - Rules, in order.
//      item        - The folder holding the messages.
//      numbers     - The messages to test.
//      folders     - Resolves move and copy destinations.
//      account_id  - Account the mail arrived for, or 0 for any.
//
// Returns:
//      How many messages left the folder.
//
int apply_rules(const std::vector<rule> &rules, mail::folder_item &item,
                const std::vector<std::uint32_t> &numbers,
                const mail::folder_list &folders, int account_id = 0);

} // namespace filter

#endif
