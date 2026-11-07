//
// The rc file format.
//
// Derived from Claws Mail src/prefs_gtk.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "prefs/rc_file.h"

#include <algorithm>

#include "util/files.h"
#include "util/text.h"

namespace prefs {
namespace {

// Return values escaped as Claws writes them: newlines become \n.
std::string escape(std::string_view value) {
    std::string out;
    for (const char c : value) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out;
}

// Undo escape.
std::string unescape(std::string_view value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 == value.size()) {
            out += value[i];
            continue;
        }
        const char next = value[++i];
        out += next == 'n' ? '\n' : next;
    }
    return out;
}

} // namespace

bool rc_section::has(std::string_view key) const {
    return std::ranges::any_of(entries_, [&](const auto &entry) {
        return entry.first == key;
    });
}

std::string rc_section::get(std::string_view key,
                            std::string_view fallback) const {
    for (const auto &[name, value] : entries_)
        if (name == key)
            return value;
    return std::string(fallback);
}

int rc_section::get_int(std::string_view key, int fallback) const {
    const auto value = util::parse_int(get(key));
    return value ? static_cast<int>(*value) : fallback;
}

bool rc_section::get_bool(std::string_view key, bool fallback) const {
    const std::string value = get(key);
    if (value == "1" || util::iequals(value, "true"))
        return true;
    if (value == "0" || util::iequals(value, "false"))
        return false;
    return fallback;
}

void rc_section::set(std::string_view key, std::string_view value) {
    for (auto &[name, stored] : entries_)
        if (name == key) {
            stored = value;
            return;
        }
    entries_.emplace_back(key, value);
}

void rc_section::set_int(std::string_view key, int value) {
    set(key, std::to_string(value));
}

void rc_section::set_bool(std::string_view key, bool value) {
    set(key, value ? "1" : "0");
}

rc_file rc_file::load(const std::filesystem::path &path) {
    const auto text = util::read_file(path);
    return text ? parse(*text) : rc_file{};
}

rc_file rc_file::parse(std::string_view text) {
    rc_file file;
    for (const std::string &line : util::split_lines(text)) {
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[' && line.back() == ']') {
            file.sections_.emplace_back(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos || file.sections_.empty())
            continue;
        file.sections_.back().set(line.substr(0, equal),
                                  unescape(std::string_view(line)
                                               .substr(equal + 1)));
    }
    return file;
}

std::string rc_file::serialize() const {
    std::string out;
    for (const rc_section &block : sections_) {
        out += "[" + block.label() + "]\n";
        for (const auto &[key, value] : block.entries())
            out += key + "=" + escape(value) + "\n";
        out += "\n";
    }
    return out;
}

bool rc_file::save(const std::filesystem::path &path,
                   bool owner_only) const {
    return util::write_file(path, serialize(), owner_only);
}

rc_section &rc_file::section(std::string_view label) {
    for (rc_section &block : sections_)
        if (block.label() == label)
            return block;
    return sections_.emplace_back(std::string(label));
}

const rc_section *rc_file::find(std::string_view label) const {
    for (const rc_section &block : sections_)
        if (block.label() == label)
            return &block;
    return nullptr;
}

void rc_file::remove(std::string_view label) {
    std::erase_if(sections_, [&](const rc_section &block) {
        return block.label() == label;
    });
}

} // namespace prefs
