//
// The command line.
//
// Derived from Claws Mail src/main.c (parse_cmd_opt),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef APP_OPTIONS_H
#define APP_OPTIONS_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/engine.h"

namespace app {

// What the command line asked for.
struct options {
    bool show_help = false;
    bool show_version = false;
    bool list_folders = false;
    bool receive = false;
    bool send_queued = false;
    std::optional<std::string> summary;     // Folder id.
    std::optional<std::string> message;     // "folder-id:number".
    std::optional<std::filesystem::path> import_claws;
    std::optional<std::string> select;      // "folder-id[:number]".
    std::optional<std::filesystem::path> open_file;
    paths where = paths::defaults();
    net::connector connect = net::socket_connector();
    std::string error;

    // Report whether the request needs no window.
    bool batch() const;
};

// Parse arguments.
options parse_options(int argc, char **argv);

// Return the usage text.
std::string usage_text();

// Run a batch request; returns the exit status.
int run_batch(const options &settings);

} // namespace app

#endif
