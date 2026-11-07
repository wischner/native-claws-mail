//
// Conditions over messages. matcher.c's bison grammar and giant switch
// become a table of criteria, one small parser and one evaluator, and
// the text syntax stays Claws', so an existing matcherrc keeps working.
//
// Derived from Claws Mail src/matcher.c, src/matcher_parser_parse.y and
// src/advsearch.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef FILTER_MATCHER_H
#define FILTER_MATCHER_H

#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mail/msg_info.h"

namespace filter {

// What a condition examines.
enum class criterion {
    all, unread, is_new, marked, deleted, replied, forwarded, locked,
    color_label, ignore_thread, watch_thread, spam, has_attachment, is_signed,
    subject, from, to, cc, to_or_cc, newsgroups, message_id, in_reply_to,
    references, age_greater, age_lower, age_greater_hours, age_lower_hours,
    size_greater, size_smaller, size_equal, partial, header, headers_part,
    message, body_part
};

// How a string criterion compares.
enum class match_kind {
    substring_nocase,   // "matchcase": despite the name, ignores case.
    substring_case,     // "match".
    regexp_nocase,      // "regexpcase".
    regexp_case         // "regexp".
};

// One condition.
struct condition {
    criterion what = criterion::all;
    bool negated = false;
    match_kind kind = match_kind::substring_nocase;
    std::string header;     // For criterion::header.
    std::string expression;
    int value = 0;

    // Return the condition in matcherrc syntax.
    std::string to_string() const;
};

// A message being tested. Raw text is fetched only when a condition
// needs it, so most rules never open the message.
struct match_input {
    const mail::msg_info &info;
    std::function<std::optional<std::string>()> raw;
    std::time_t now = 0;
};

// Conditions joined by one operator.
struct condition_list {
    std::vector<condition> conditions;
    bool match_all = true;  // "&" rather than "|".

    // Report whether a message matches.
    bool matches(const match_input &input) const;

    // Report whether matching must read message text, not just summaries.
    bool needs_content() const;

    // Return the list in matcherrc syntax.
    std::string to_string() const;
};

// Walks matcher syntax.
class tokenizer {
public:
    explicit tokenizer(std::string_view text) : text_(text) {}

    // Report whether only white space remains.
    bool at_end();

    // Return the next bare word.
    std::string word();

    // Return the next word without consuming it.
    std::string peek();

    // Return a quoted string.
    std::optional<std::string> quoted();

    // Return an integer.
    std::optional<int> number();

private:
    std::string_view text_;
    std::size_t position_ = 0;
};

//
// Parse one condition.
//
// Returns:
//      The condition, or nothing if the input does not hold one; the
//      tokenizer has then advanced an undefined distance.
//
std::optional<condition> parse_condition(tokenizer &tokens);

// Parse conditions joined by & or |, stopping before the first word
// that is not a condition.
std::optional<condition_list> parse_condition_list(tokenizer &tokens);

//
// Turn a quick search string into a condition list.
//
// Parameters:
//      text        - Text in the search box.
//
// Notes:
//      Plain text searches sender and subject. Text starting with a
//      known abbreviation uses Claws' extended form, such as
//      "f alice & s report" or "U ~T".
//
// Returns:
//      The conditions, or nothing if extended text does not parse.
//
std::optional<condition_list> parse_quick_search(std::string_view text);

} // namespace filter

#endif
