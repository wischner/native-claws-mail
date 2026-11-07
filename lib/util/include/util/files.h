//
// File access for the stores: whole-file reads, crash-safe writes and
// the user's directories. A folder or configuration write must never
// leave a half-written file, so everything writes a sibling and renames.
//
// Derived from Claws Mail src/common/file-utils.c and src/common/utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_FILES_H
#define UTIL_FILES_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace util {

// Return a file's bytes, or nothing if it cannot be read.
std::optional<std::string> read_file(const std::filesystem::path &path);

//
// Replace a file atomically.
//
// Parameters:
//      path        - Destination.
//      data        - New contents.
//      owner_only  - Restrict the file to its owner, for secrets.
//
// Returns:
//      Whether the file now holds the data. On failure the previous
//      file is untouched.
//
bool write_file(const std::filesystem::path &path, std::string_view data,
                bool owner_only = false);

// Return the user's home directory.
std::filesystem::path home_directory();

// Expand a leading ~/ against the home directory.
std::filesystem::path expand_home(std::string_view path);

} // namespace util

#endif
