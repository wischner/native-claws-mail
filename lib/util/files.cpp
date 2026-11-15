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
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif
#include <stdexcept>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace util {

namespace fs = std::filesystem;

namespace {
int process_id() {
#ifdef _WIN32
    return ::_getpid();
#else
    return ::getpid();
#endif
}
} // namespace

std::optional<std::string> read_file(const fs::path &path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) return std::nullopt;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad())
        return std::nullopt;
    return std::move(buffer).str();
}

bool ensure_private_directory(const fs::path &path) {
    if (path.empty()) return true;
    std::error_code error;
    if (fs::exists(path, error)) return !error && fs::is_directory(path, error);
    if (error || !ensure_private_directory(path.parent_path())) return false;
    // mkdir's mode is applied at creation, before any content is written.
#ifdef _WIN32
    const int result = ::_wmkdir(path.c_str());
#else
    const int result = ::mkdir(path.c_str(), 0700);
#endif
    return result == 0 || (errno == EEXIST && fs::is_directory(path, error));
}

bool write_file(const fs::path &path, std::string_view data, bool owner_only) {
    static std::atomic<unsigned> serial{0};
    if (!ensure_private_directory(path.parent_path())) return false;
    fs::path scratch;
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        scratch = path;
        scratch += ".tmp" + std::to_string(process_id()) + "-" + std::to_string(++serial);
#ifdef _WIN32
        // Windows inherits the user's directory ACL; retain Unicode paths.
        fd = ::_wopen(scratch.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                      _S_IREAD | _S_IWRITE);
        (void)owner_only;
#else
        fd = ::open(scratch.c_str(), O_WRONLY | O_CREAT | O_EXCL, owner_only ? 0600 : 0644);
#endif
        if (fd < 0 && errno != EEXIST) return false;
    }
    if (fd < 0) return false;
    bool ok = true;
    while (!data.empty()) {
#ifdef _WIN32
        const auto count = ::_write(fd, data.data(),
            static_cast<unsigned>(std::min<std::size_t>(data.size(), 1024 * 1024)));
#else
        const auto count = ::write(fd, data.data(), data.size());
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        data.remove_prefix(static_cast<std::size_t>(count));
    }
#ifdef _WIN32
    if (::_commit(fd) != 0) ok = false;
    if (::_close(fd) != 0) ok = false;
#else
    if (::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
#endif
    std::error_code error;
    if (ok) {
#ifdef _WIN32
        ok = ::MoveFileExW(scratch.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        fs::rename(scratch, path, error);
        ok = !error;
#endif
    }
    if (!ok) fs::remove(scratch, error);
    return ok;
}

fs::path home_directory() {
    if (const char *home = std::getenv("HOME"); home && fs::path(home).is_absolute())
        return home;
#ifdef _WIN32
    if (const char *home = std::getenv("USERPROFILE"); home && fs::path(home).is_absolute())
        return home;
#else
    if (const auto *entry = ::getpwuid(::getuid()); entry && entry->pw_dir)
        return entry->pw_dir;
#endif
    throw std::runtime_error("Cannot determine the user's home directory.");
}

fs::path user_directory(const char *variable, const fs::path &fallback) {
    if (const char *value = std::getenv(variable); value && fs::path(value).is_absolute())
        return fs::path(value) / "blaise";
    return home_directory() / fallback / "blaise";
}

fs::path expand_home(std::string_view path) {
    if (path == "~")
        return home_directory();
    if (path.starts_with("~/"))
        return home_directory() / path.substr(2);
    return path;
}

} // namespace util
