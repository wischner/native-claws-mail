//
// File access.
//
// Derived from Claws Mail src/common/file-utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/files.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace util {

namespace fs = std::filesystem;

std::optional<std::string> read_file(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad())
        return std::nullopt;
    return std::move(buffer).str();
}

bool write_file(const fs::path &path, std::string_view data,
                bool owner_only) {
    static std::atomic<unsigned> serial{0};
    std::error_code error;
    if (path.has_parent_path())
        fs::create_directories(path.parent_path(), error);
    fs::path scratch = path;
    scratch += ".tmp" + std::to_string(++serial);
    {
        std::ofstream stream(scratch, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;
        if (owner_only)
            fs::permissions(scratch,
                            fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace, error);
        stream.write(data.data(),
                     static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) {
            stream.close();
            fs::remove(scratch, error);
            return false;
        }
    }
    fs::rename(scratch, path, error);
    if (error) {
        std::error_code ignored;
        fs::remove(scratch, ignored);
        return false;
    }
    return true;
}

fs::path home_directory() {
    if (const char *home = std::getenv("HOME"); home != nullptr && *home)
        return home;
    return fs::current_path();
}

fs::path expand_home(std::string_view path) {
    if (path == "~")
        return home_directory();
    if (path.starts_with("~/"))
        return home_directory() / path.substr(2);
    return path;
}

} // namespace util
