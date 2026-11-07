//
// The MH store.
//
// Derived from Claws Mail src/mh.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/mh_folder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

#include <sys/stat.h>

#include "util/files.h"
#include "util/text.h"

namespace mail {
namespace {

namespace fs = std::filesystem;

// One folder every store has.
struct standard_folder {
    const char *name;
    item_type type;
};

constexpr std::array<standard_folder, 5> standard_folders{{
    {"inbox", item_type::inbox},
    {"sent", item_type::outbox},
    {"queue", item_type::queue},
    {"draft", item_type::draft},
    {"trash", item_type::trash},
}};

// Report whether a directory entry is a message.
bool is_message_name(const std::string &name) {
    return !name.empty() &&
           std::ranges::all_of(name, [](char c) {
               return std::isdigit(static_cast<unsigned char>(c)) != 0;
           }) &&
           name.size() < 11 && name[0] != '0';
}

// Return a file's change time as seconds since the epoch. This must be
// exact: it is compared with the stored time to spot changed messages,
// and converting from std::filesystem's clock is not exact on every
// standard library.
std::time_t file_time(const fs::path &path) {
#ifdef _WIN32
    struct _stat64 info {};
    return ::_wstat64(path.c_str(), &info) == 0 ? info.st_mtime : 0;
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 ? info.st_mtime : 0;
#endif
}

// Return a type for a top-level directory by its conventional name.
item_type type_for(const std::string &name) {
    for (const standard_folder &entry : standard_folders)
        if (name == entry.name)
            return entry.type;
    return item_type::normal;
}

} // namespace

mh_folder::mh_folder(std::string name, fs::path directory)
    : folder(std::move(name)), directory_(std::move(directory)) {
    std::error_code error;
    fs::create_directories(directory_, error);
    for (const standard_folder &entry : standard_folders)
        fs::create_directories(directory_ / entry.name, error);
    scan_tree();
}

fs::path mh_folder::item_path(const folder_item &item) const {
    return item.path.empty() ? directory_ : directory_ / item.path;
}

fs::path mh_folder::cache_directory(const folder_item &item) const {
    return item_path(item);
}

std::vector<std::uint32_t> mh_folder::list_numbers(folder_item &item) {
    std::vector<std::uint32_t> numbers;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(item_path(item), error)) {
        const std::string name = entry.path().filename().string();
        if (!is_message_name(name))
            continue;
        if (const auto number = util::parse_int(name))
            numbers.push_back(static_cast<std::uint32_t>(*number));
    }
    std::ranges::sort(numbers);
    return numbers;
}

std::optional<msg_info> mh_folder::read_info(folder_item &item,
                                             std::uint32_t number) {
    const fs::path path = item_path(item) / std::to_string(number);
    // Headers rarely pass this size; the summary needs nothing further.
    constexpr std::size_t header_window = 256 * 1024;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::nullopt;
    std::string head(header_window, '\0');
    stream.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(stream.gcount()));
    msg_info info = parse_msg_info(head, number);
    std::error_code error;
    info.size = fs::file_size(path, error);
    return info;
}

std::optional<std::string> mh_folder::fetch(folder_item &item,
                                            std::uint32_t number) {
    return util::read_file(item_path(item) / std::to_string(number));
}

std::uint32_t mh_folder::add(folder_item &item, std::string_view raw,
                             std::uint32_t) {
    const auto numbers = list_numbers(item);
    std::uint32_t next = numbers.empty() ? 1 : numbers.back() + 1;
    if (const msg_map &known = item.messages(); !known.empty())
        next = std::max(next, known.rbegin()->first + 1);
    const fs::path path = item_path(item) / std::to_string(next);
    return util::write_file(path, raw) ? next : 0;
}

bool mh_folder::remove(folder_item &item, std::uint32_t number) {
    std::error_code error;
    fs::remove(item_path(item) / std::to_string(number), error);
    return !error;
}

std::time_t mh_folder::message_time(folder_item &item,
                                    std::uint32_t number) {
    return file_time(item_path(item) / std::to_string(number));
}

folder_item *mh_folder::create_folder(folder_item &parent,
                                      const std::string &name) {
    if (name.empty() || name.find('/') != std::string::npos ||
        name == "." || name == "..")
        return nullptr;
    const std::string path =
        parent.path.empty() ? name : parent.path + "/" + name;
    std::error_code error;
    if (fs::exists(directory_ / path, error))
        return nullptr;
    if (!fs::create_directories(directory_ / path, error))
        return nullptr;
    parent.children.push_back(
        std::make_unique<folder_item>(*this, &parent, name, path));
    return parent.children.back().get();
}

bool mh_folder::rename_folder(folder_item &item, const std::string &name) {
    if (item.parent() == nullptr || name.empty() ||
        name.find('/') != std::string::npos)
        return false;
    const folder_item *parent = item.parent();
    const std::string path =
        parent->path.empty() ? name : parent->path + "/" + name;
    std::error_code error;
    fs::rename(item_path(item), directory_ / path, error);
    if (error)
        return false;
    item.name = name;
    item.set_path(path);
    return true;
}

bool mh_folder::remove_folder(folder_item &item) {
    if (item.parent() == nullptr)
        return false;
    std::error_code error;
    fs::remove_all(item_path(item), error);
    if (error)
        return false;
    detach_item(item);
    return true;
}

void mh_folder::scan_tree() { scan_directory(root()); }

void mh_folder::scan_directory(folder_item &parent) {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto &entry :
         fs::directory_iterator(item_path(parent), error)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(error) && !name.starts_with('.') &&
            !is_message_name(name))
            names.push_back(name);
    }
    std::ranges::sort(names);
    for (const std::string &name : names) {
        const std::string path =
            parent.path.empty() ? name : parent.path + "/" + name;
        const bool known = std::ranges::any_of(
            parent.children,
            [&](const auto &child) { return child->path == path; });
        if (!known)
            parent.children.push_back(std::make_unique<folder_item>(
                *this, &parent, name, path,
                parent.parent() == nullptr ? type_for(name)
                                           : item_type::normal));
    }
    std::erase_if(parent.children, [&](const auto &child) {
        return !fs::exists(item_path(*child), error);
    });
    // Standard folders first, in their usual order; then alphabetical.
    std::ranges::stable_sort(
        parent.children, {}, [](const std::unique_ptr<folder_item> &item) {
            return item->type == item_type::normal
                       ? 99
                       : static_cast<int>(item->type);
        });
    for (const auto &child : parent.children)
        scan_directory(*child);
}

} // namespace mail
