//
// Moving mail and settings in and out: mbox files, and adopting a Claws
// Mail configuration.
//
// Derived from Claws Mail src/mbox.c and src/main.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "app/engine.h"
#include "mail/mbox.h"
#include "util/files.h"

namespace app {

namespace fs = std::filesystem;

void engine::import_mbox(const fs::path &file, const std::string &id) {
    submit([this, file, id] {
        const auto text = util::read_file(file);
        if (!text)
            throw std::runtime_error("Could not read " + file.string());
        mail::folder_item *item = require(id);
        int added = 0;
        for (const std::string &message : mail::split_mbox(*text))
            added += item->owner().add(*item, message, 0) != 0 ? 1 : 0;
        item->scan();
        publish_folder(*item);
        publish_tree();
        const std::string note = std::to_string(added) + " messages imported";
        jobs_.deliver([this, note] { listener_.status(note); });
    });
}

void engine::export_mbox(const std::string &id, const fs::path &file) {
    submit([this, id, file] {
        mail::folder_item *item = require(id);
        std::string out;
        for (const auto &[number, info] : item->messages())
            if (const auto raw = item->owner().fetch(*item, number))
                mail::append_mbox(out, *raw, info.from_name, info.date_time);
        if (!util::write_file(file, out))
            throw std::runtime_error("Could not write " + file.string());
        const std::string note = "Exported " + item->name;
        jobs_.deliver([this, note] { listener_.status(note); });
    });
}

bool engine::import_claws(const fs::path &claws_directory,
                          const fs::path &config) {
    std::error_code error;
    if (!fs::is_directory(claws_directory, error) ||
        fs::exists(config / "accountrc", error))
        return false;
    if (error || !util::ensure_private_directory(config)) return false;
    bool any = false;
    const auto copy = [&](const fs::path &source, const fs::path &target) {
        const auto bytes = util::read_file(source);
        return bytes && util::write_file(target, *bytes, true);
    };
    const fs::path books = claws_directory / "addrbook";
    if (fs::is_directory(books, error)) {
        for (const auto &entry : fs::directory_iterator(books, error)) {
            if (!entry.is_regular_file(error) || error) return false;
            const auto target = config / "addrbook" / entry.path().filename();
            if (!fs::exists(target, error) && !copy(entry.path(), target)) return false;
            if (error) return false;
        }
    }
    if (error) return false;
    // Publish accounts last, so a failed import can be retried.
    for (const char *name : {"matcherrc", "folderlist.xml", "accountrc"}) {
        if (fs::exists(claws_directory / name, error)) {
            if (!copy(claws_directory / name, config / name)) return false;
            if (std::string_view(name) != "matcherrc") any = true;
        }
        if (error) return false;
    }
    return any;
}

} // namespace app
