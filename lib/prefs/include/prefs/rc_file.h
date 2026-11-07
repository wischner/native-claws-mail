//
// The configuration file format Claws uses for accountrc and friends:
// "[label]" sections of key=value lines. The file object keeps every
// line it does not understand, so Blaise can rewrite a file that Claws
// wrote without losing settings it has not ported.
//
// Derived from Claws Mail src/prefs_gtk.c and src/common/prefs.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef PREFS_RC_FILE_H
#define PREFS_RC_FILE_H

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prefs {

// One labelled block of key=value settings, in file order.
class rc_section {
public:
    // Create a section.
    explicit rc_section(std::string label = {}) : label_(std::move(label)) {}

    // Return the text between the brackets.
    const std::string &label() const { return label_; }

    // Report whether a key is present.
    bool has(std::string_view key) const;

    // Return a value, or the fallback when the key is absent.
    std::string get(std::string_view key,
                    std::string_view fallback = {}) const;

    // Return an integer value, or the fallback.
    int get_int(std::string_view key, int fallback = 0) const;

    // Return a boolean stored as 0 or 1, TRUE or FALSE.
    bool get_bool(std::string_view key, bool fallback = false) const;

    // Set a value, keeping the key's place when it exists.
    void set(std::string_view key, std::string_view value);

    // Set an integer value.
    void set_int(std::string_view key, int value);

    // Set a boolean value in the 0 or 1 form Claws writes.
    void set_bool(std::string_view key, bool value);

    // Return all entries in order.
    const std::vector<std::pair<std::string, std::string>> &entries() const {
        return entries_;
    }

private:
    std::string label_;
    std::vector<std::pair<std::string, std::string>> entries_;
};

// A whole configuration file.
class rc_file {
public:
    // Read a file; a missing file reads as empty.
    static rc_file load(const std::filesystem::path &path);

    // Parse file text.
    static rc_file parse(std::string_view text);

    // Return the file text.
    std::string serialize() const;

    // Write the file atomically; returns whether it succeeded.
    bool save(const std::filesystem::path &path,
              bool owner_only = false) const;

    // Return a section by label, creating it at the end when absent.
    rc_section &section(std::string_view label);

    // Return a section by label, or null.
    const rc_section *find(std::string_view label) const;

    // Remove a section.
    void remove(std::string_view label);

    // Return every section in order.
    const std::vector<rc_section> &sections() const { return sections_; }

private:
    std::vector<rc_section> sections_;
};

} // namespace prefs

#endif
