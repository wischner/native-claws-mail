//
// Conditions over messages.
//
// Derived from Claws Mail src/matcher.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "filter/matcher.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <regex>

#include "mime/header.h"
#include "mime/part.h"
#include "util/text.h"

namespace filter {
namespace {

// What follows a criterion's keyword.
enum class argument { none, text, number, header_text };

// One keyword.
struct criterion_entry {
    const char *name;
    criterion what;
    argument takes;
};

constexpr std::array<criterion_entry, 35> criteria{{
    {"all", criterion::all, argument::none},
    {"unread", criterion::unread, argument::none},
    {"new", criterion::is_new, argument::none},
    {"marked", criterion::marked, argument::none},
    {"deleted", criterion::deleted, argument::none},
    {"replied", criterion::replied, argument::none},
    {"forwarded", criterion::forwarded, argument::none},
    {"locked", criterion::locked, argument::none},
    {"colorlabel", criterion::color_label, argument::number},
    {"ignore_thread", criterion::ignore_thread, argument::none},
    {"watch_thread", criterion::watch_thread, argument::none},
    {"spam", criterion::spam, argument::none},
    {"has_attachment", criterion::has_attachment, argument::none},
    {"signed", criterion::is_signed, argument::none},
    {"subject", criterion::subject, argument::text},
    {"from", criterion::from, argument::text},
    {"to", criterion::to, argument::text},
    {"cc", criterion::cc, argument::text},
    {"to_or_cc", criterion::to_or_cc, argument::text},
    {"newsgroups", criterion::newsgroups, argument::text},
    {"messageid", criterion::message_id, argument::text},
    {"inreplyto", criterion::in_reply_to, argument::text},
    {"references", criterion::references, argument::text},
    {"age_greater", criterion::age_greater, argument::number},
    {"age_lower", criterion::age_lower, argument::number},
    {"age_greater_hours", criterion::age_greater_hours, argument::number},
    {"age_lower_hours", criterion::age_lower_hours, argument::number},
    {"size_greater", criterion::size_greater, argument::number},
    {"size_smaller", criterion::size_smaller, argument::number},
    {"size_equal", criterion::size_equal, argument::number},
    {"partial", criterion::partial, argument::none},
    {"header", criterion::header, argument::header_text},
    {"headers_part", criterion::headers_part, argument::text},
    {"message", criterion::message, argument::text},
    {"body_part", criterion::body_part, argument::text},
}};

constexpr std::array<const char *, 4> kind_names{"matchcase", "match",
                                                 "regexpcase", "regexp"};

// Return a table entry by keyword, or null.
const criterion_entry *find_criterion(std::string_view name) {
    for (const criterion_entry &entry : criteria)
        if (name == entry.name)
            return &entry;
    return nullptr;
}

// Return a table entry by value.
const criterion_entry &entry_for(criterion what) {
    for (const criterion_entry &entry : criteria)
        if (entry.what == what)
            return entry;
    return criteria.front();
}

// Return text quoted for matcherrc.
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// Compare text with a condition's expression.
bool text_matches(const condition &test, std::string_view text) {
    switch (test.kind) {
    case match_kind::substring_nocase:
        return util::ifind(text, test.expression) != std::string_view::npos;
    case match_kind::substring_case:
        return text.find(test.expression) != std::string_view::npos;
    case match_kind::regexp_nocase:
    case match_kind::regexp_case:
        break;
    }
    try {
        auto flags = std::regex::ECMAScript;
        if (test.kind == match_kind::regexp_nocase)
            flags |= std::regex::icase;
        const std::regex pattern(test.expression, flags);
        return std::regex_search(text.begin(), text.end(), pattern);
    } catch (const std::regex_error &) {
        return false; // A bad pattern matches nothing, as in Claws.
    }
}

// Return a message's headers in display form.
std::string headers_text(const mime::part &root) {
    std::string out;
    for (const mime::header_field &field : root.headers.fields())
        out += field.name + ": " + field.value + "\n";
    return out;
}

// Evaluate a condition that needs the message text.
bool test_content(const condition &test, const match_input &input) {
    const auto raw = input.raw ? input.raw() : std::nullopt;
    if (!raw)
        return false;
    const mime::part root = mime::parse_message(*raw);
    switch (test.what) {
    case criterion::header:
        for (const std::string &value : root.headers.get_all(test.header))
            if (text_matches(test, value))
                return true;
        return false;
    case criterion::headers_part:
        return text_matches(test, headers_text(root));
    case criterion::body_part:
        return text_matches(test, mime::body_text(root, *raw));
    default:
        return text_matches(test, headers_text(root) +
                                      mime::body_text(root, *raw));
    }
}

// Return a message's age in seconds.
std::time_t age_of(const match_input &input) {
    const std::time_t now = input.now != 0 ? input.now : std::time(nullptr);
    return now - input.info.date_time;
}

// Evaluate one condition without its negation.
bool test_one(const condition &test, const match_input &input) {
    const mail::msg_info &info = input.info;
    const std::time_t age = age_of(input);
    switch (test.what) {
    case criterion::all:
        return true;
    case criterion::unread:
        return info.has(mail::flag_unread);
    case criterion::is_new:
        return info.has(mail::flag_new);
    case criterion::marked:
        return info.has(mail::flag_marked);
    case criterion::deleted:
        return info.has(mail::flag_deleted);
    case criterion::replied:
        return info.has(mail::flag_replied);
    case criterion::forwarded:
        return info.has(mail::flag_forwarded);
    case criterion::locked:
        return info.has(mail::flag_locked);
    case criterion::color_label:
        return info.color_label() == test.value;
    case criterion::ignore_thread:
        return info.has(mail::flag_ignore_thread);
    case criterion::watch_thread:
        return info.has(mail::flag_watch_thread);
    case criterion::spam:
        return info.has(mail::flag_spam);
    case criterion::has_attachment:
        return (info.temp_flags & mail::temp_has_attachment) != 0;
    case criterion::is_signed:
        return (info.temp_flags & mail::temp_signed) != 0;
    case criterion::subject:
        return text_matches(test, info.subject);
    case criterion::from:
        return text_matches(test, info.from);
    case criterion::to:
        return text_matches(test, info.to);
    case criterion::cc:
        return text_matches(test, info.cc);
    case criterion::to_or_cc:
        return text_matches(test, info.to) || text_matches(test, info.cc);
    case criterion::newsgroups:
        return text_matches(test, info.newsgroups);
    case criterion::message_id:
        return text_matches(test, info.message_id);
    case criterion::in_reply_to:
        return text_matches(test, info.in_reply_to);
    case criterion::references:
        return text_matches(test, util::join(info.references, " "));
    case criterion::age_greater:
        return age >= test.value * 86400;
    case criterion::age_lower:
        return age < test.value * 86400;
    case criterion::age_greater_hours:
        return age >= test.value * 3600;
    case criterion::age_lower_hours:
        return age < test.value * 3600;
    case criterion::size_greater:
        return info.size > static_cast<std::uint64_t>(test.value);
    case criterion::size_smaller:
        return info.size < static_cast<std::uint64_t>(test.value);
    case criterion::size_equal:
        return info.size == static_cast<std::uint64_t>(test.value);
    case criterion::partial:
        return info.total_size != 0;
    case criterion::header:
    case criterion::headers_part:
    case criterion::message:
    case criterion::body_part:
        return test_content(test, input);
    }
    return false;
}

// An abbreviation of the extended quick search.
struct abbreviation {
    const char *text;
    criterion what;
    bool negated;
};

constexpr std::array<abbreviation, 30> abbreviations{{
    {"a", criterion::all, false},
    {"ag", criterion::age_greater, false},
    {"al", criterion::age_lower, false},
    {"agh", criterion::age_greater_hours, false},
    {"alh", criterion::age_lower_hours, false},
    {"b", criterion::body_part, false},
    {"B", criterion::message, false},
    {"c", criterion::cc, false},
    {"C", criterion::to_or_cc, false},
    {"D", criterion::deleted, false},
    {"f", criterion::from, false},
    {"F", criterion::forwarded, false},
    {"h", criterion::headers_part, false},
    {"ha", criterion::has_attachment, false},
    {"i", criterion::message_id, false},
    {"I", criterion::in_reply_to, false},
    {"k", criterion::color_label, false},
    {"L", criterion::locked, false},
    {"n", criterion::newsgroups, false},
    {"N", criterion::is_new, false},
    {"O", criterion::is_new, true},
    {"r", criterion::replied, false},
    {"R", criterion::unread, true},
    {"s", criterion::subject, false},
    {"Sg", criterion::size_greater, false},
    {"Ss", criterion::size_smaller, false},
    {"t", criterion::to, false},
    {"T", criterion::marked, false},
    {"U", criterion::unread, false},
    {"x", criterion::references, false},
}};

// Return an abbreviation, or null.
const abbreviation *find_abbreviation(std::string_view word) {
    for (const abbreviation &entry : abbreviations)
        if (word == entry.text)
            return &entry;
    return nullptr;
}

// Report whether a search string uses the extended form.
bool is_extended(std::string_view text) {
    tokenizer tokens(text);
    std::string first = tokens.word();
    while (!first.empty() &&
           (first[0] == '~' || first[0] == '!' || first[0] == '%' ||
            first[0] == '#'))
        first.erase(0, 1);
    return find_abbreviation(first) != nullptr;
}

// Parse the extended quick search form.
std::optional<condition_list> parse_extended(std::string_view text) {
    tokenizer tokens(text);
    condition_list list;
    while (!tokens.at_end()) {
        std::string word = tokens.word();
        if (word == "&" || word == "|") {
            list.match_all = word == "&";
            continue;
        }
        condition test;
        for (; !word.empty(); word.erase(0, 1)) {
            if (word[0] == '~' || word[0] == '!')
                test.negated = !test.negated;
            else if (word[0] == '%')
                test.kind = test.kind == match_kind::regexp_nocase
                                ? match_kind::regexp_case
                                : match_kind::substring_case;
            else if (word[0] == '#')
                test.kind = test.kind == match_kind::substring_case
                                ? match_kind::regexp_case
                                : match_kind::regexp_nocase;
            else
                break;
        }
        const abbreviation *entry = find_abbreviation(word);
        if (entry == nullptr)
            return std::nullopt;
        test.what = entry->what;
        test.negated = test.negated != entry->negated;
        const argument takes = entry_for(entry->what).takes;
        if (takes == argument::text) {
            test.expression = tokens.word();
            if (test.expression.empty())
                return std::nullopt;
        } else if (takes == argument::number) {
            const auto value = tokens.number();
            if (!value)
                return std::nullopt;
            test.value = *value;
        }
        list.conditions.push_back(std::move(test));
    }
    if (list.conditions.empty())
        return std::nullopt;
    return list;
}

} // namespace

std::string condition::to_string() const {
    const criterion_entry &entry = entry_for(what);
    std::string out = negated ? "~" : "";
    out += entry.name;
    switch (entry.takes) {
    case argument::none:
        break;
    case argument::number:
        out += " " + std::to_string(value);
        break;
    case argument::header_text:
        out += " " + quote(header);
        [[fallthrough]];
    case argument::text:
        out += " ";
        out += kind_names[static_cast<std::size_t>(kind)];
        out += " " + quote(expression);
        break;
    }
    return out;
}

bool condition_list::matches(const match_input &input) const {
    for (const condition &test : conditions) {
        const bool hit = test_one(test, input) != test.negated;
        if (hit != match_all)
            return hit;
    }
    return match_all;
}

bool condition_list::needs_content() const {
    return std::ranges::any_of(conditions, [](const condition &test) {
        return test.what == criterion::header ||
               test.what == criterion::headers_part ||
               test.what == criterion::message ||
               test.what == criterion::body_part;
    });
}

std::string condition_list::to_string() const {
    std::string out;
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        if (i != 0)
            out += match_all ? " & " : " | ";
        out += conditions[i].to_string();
    }
    return out;
}

bool tokenizer::at_end() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_])))
        ++position_;
    return position_ >= text_.size();
}

std::string tokenizer::word() {
    if (at_end())
        return {};
    if (text_[position_] == '"')
        return quoted().value_or("");
    const std::size_t start = position_;
    while (position_ < text_.size() &&
           !std::isspace(static_cast<unsigned char>(text_[position_])))
        ++position_;
    return std::string(text_.substr(start, position_ - start));
}

std::string tokenizer::peek() {
    const std::size_t saved = position_;
    std::string result = word();
    position_ = saved;
    return result;
}

std::optional<std::string> tokenizer::quoted() {
    if (at_end() || text_[position_] != '"')
        return std::nullopt;
    ++position_;
    std::string out;
    while (position_ < text_.size() && text_[position_] != '"') {
        if (text_[position_] == '\\' && position_ + 1 < text_.size())
            ++position_;
        out += text_[position_++];
    }
    if (position_ >= text_.size())
        return std::nullopt;
    ++position_;
    return out;
}

std::optional<int> tokenizer::number() {
    const auto value = util::parse_int(word());
    if (!value)
        return std::nullopt;
    return static_cast<int>(*value);
}

std::optional<condition> parse_condition(tokenizer &tokens) {
    std::string name = tokens.word();
    condition test;
    if (name.starts_with('~')) {
        test.negated = true;
        name.erase(0, 1);
    }
    const criterion_entry *entry = find_criterion(name);
    if (entry == nullptr)
        return std::nullopt;
    test.what = entry->what;
    switch (entry->takes) {
    case argument::none:
        return test;
    case argument::number: {
        const auto value = tokens.number();
        if (!value)
            return std::nullopt;
        test.value = *value;
        return test;
    }
    case argument::header_text: {
        const auto field = tokens.quoted();
        if (!field)
            return std::nullopt;
        test.header = *field;
        [[fallthrough]];
    }
    case argument::text:
        break;
    }
    const std::string kind = tokens.word();
    bool known = false;
    for (std::size_t i = 0; i < kind_names.size(); ++i)
        if (kind == kind_names[i]) {
            test.kind = static_cast<match_kind>(i);
            known = true;
        }
    if (!known)
        return std::nullopt;
    const auto expression = tokens.quoted();
    if (!expression)
        return std::nullopt;
    test.expression = *expression;
    return test;
}

std::optional<condition_list> parse_condition_list(tokenizer &tokens) {
    condition_list list;
    while (true) {
        auto test = parse_condition(tokens);
        if (!test)
            return std::nullopt;
        list.conditions.push_back(std::move(*test));
        const std::string next = tokens.peek();
        if (next != "&" && next != "|")
            return list;
        list.match_all = next == "&";
        tokens.word();
    }
}

std::optional<condition_list> parse_quick_search(std::string_view text) {
    text = util::trim(text);
    if (text.empty())
        return std::nullopt;
    if (is_extended(text))
        if (auto extended = parse_extended(text))
            return extended;
    condition_list list;
    list.match_all = false;
    for (const criterion what : {criterion::subject, criterion::from}) {
        condition test;
        test.what = what;
        test.expression = text;
        list.conditions.push_back(std::move(test));
    }
    return list;
}

} // namespace filter
