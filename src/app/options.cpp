//
// The command line, and the work that needs no window.
//
// Derived from Claws Mail src/main.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "app/options.h"

#include <exception>
#include <iostream>

#include "util/date.h"
#include "util/text.h"
#include "view/document.h"
#include "view/summary.h"

namespace app {
namespace {

// Prints what the engine reports.
class console_listener final : public engine_listener {
public:
    int failures = 0;
    std::vector<folder_node> tree;
    folder_view shown;

    void folders_changed(const std::vector<folder_node> &value) override {
        tree = value;
    }
    void folder_loaded(const folder_view &view) override { shown = view; }
    void status(const std::string &text) override {
        std::cerr << text << '\n';
    }
    void failure(const std::string &text) override {
        std::cerr << "blaise: " << text << '\n';
        ++failures;
    }
    void mail_received(int count) override {
        std::cerr << count << " new messages\n";
    }
};

// Print one tree branch.
void print_node(const folder_node &node, int depth) {
    std::cout << std::string(static_cast<std::size_t>(depth) * 2, ' ')
              << node.id << "  (" << node.unread_count << "/"
              << node.total_count << ")\n";
    for (const folder_node &child : node.children)
        print_node(child, depth + 1);
}

// Print a folder's messages.
void print_summary(const folder_view &view) {
    view::summary_model model;
    model.set_sort(view.sort, view.sort_ascending);
    model.set_messages(view.messages);
    for (std::size_t i = 0; i < model.rows().size(); ++i) {
        const mail::msg_info &info = model.at(i);
        std::cout << info.number << '\t' << view::status_text(info) << '\t'
                  << util::format_list_date(info.date_time) << '\t'
                  << info.from_name << '\t' << info.subject << '\n';
    }
}

} // namespace

bool options::batch() const {
    return list_folders || receive || send_queued || summary || message ||
           import_claws;
}

options parse_options(int argc, char **argv) {
    options settings;
    for (int i = 1; i < argc; ++i) {
        const std::string word = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 < argc)
                return argv[++i];
            settings.error = word + " needs a value";
            return {};
        };
        if (word == "--help" || word == "-h")
            settings.show_help = true;
        else if (word == "--version" || word == "-v")
            settings.show_version = true;
        else if (word == "--config")
            settings.where.config = value();
        else if (word == "--mail")
            settings.where.mail = value();
        else if (word == "--list")
            settings.list_folders = true;
        else if (word == "--summary")
            settings.summary = value();
        else if (word == "--message")
            settings.message = value();
        else if (word == "--receive")
            settings.receive = true;
        else if (word == "--send")
            settings.send_queued = true;
        else if (word == "--select")
            settings.select = value();
        else if (word == "--import-claws")
            settings.import_claws = value();
        else if (!word.empty() && word[0] == '-')
            settings.error = "unknown option " + word;
        else
            settings.open_file = word;
    }
    return settings;
}

std::string usage_text() {
    return "usage: blaise [options] [message.eml]\n"
           "  --config DIR        configuration directory (~/.blaise)\n"
           "  --mail DIR          mail directory (~/Mail)\n"
           "  --select ID[:NUM]   open at a folder or message\n"
           "  --list              list folders\n"
           "  --summary FOLDER    list a folder's messages\n"
           "  --message ID:NUM    print a message\n"
           "  --receive           collect mail\n"
           "  --send              send the queue\n"
           "  --import-claws DIR  copy a Claws Mail configuration\n"
           "  --version, --help\n";
}

int run_batch(const options &settings) {
    if (settings.import_claws) {
        if (!engine::import_claws(*settings.import_claws,
                                  settings.where.config)) {
            std::cerr << "blaise: nothing to import, or already configured\n";
            return 1;
        }
        std::cout << "Imported. Passwords are not copied; set them under "
                     "Tools, Accounts.\n";
        return 0;
    }
    inline_executor jobs;
    console_listener listener;
    engine core(settings.where, jobs, listener);
    core.load();
    if (settings.receive)
        core.receive_all();
    if (settings.send_queued)
        core.send_queue();
    if (settings.list_folders)
        for (const folder_node &node : listener.tree)
            print_node(node, 0);
    if (settings.summary) {
        core.open_folder(*settings.summary);
        print_summary(listener.shown);
    }
    if (settings.message) {
        const std::size_t colon = settings.message->rfind(':');
        const auto number =
            colon == std::string::npos
                ? std::nullopt
                : util::parse_int(settings.message->substr(colon + 1));
        if (!number) {
            std::cerr << "blaise: --message needs FOLDER:NUMBER\n";
            return 2;
        }
        core.fetch_message(settings.message->substr(0, colon),
                           static_cast<std::uint32_t>(*number),
                           [&](std::optional<std::string> raw) {
                               if (!raw) {
                                   ++listener.failures;
                                   return;
                               }
                               std::cout << view::render_document(*raw).text;
                           });
    }
    core.shut_down();
    return listener.failures == 0 ? 0 : 1;
}

} // namespace app
