//
// Drives the real application window through Native's public input
// entry points, with the engine on its real worker thread. Results come
// back asynchronously, so the scenario is a list of steps, each waiting
// on a condition. Nothing here names a toolkit, so one source runs on
// every Native backend; a backend supplies only its display.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "check.h"
#include "fake_server.h"
#include "main_window.h"
#include "mime/part.h"
#include "tool_icons.h"
#include "util/files.h"

namespace fs = std::filesystem;
using key_code = native::key_code;

namespace {

// A scratch configuration with some mail in it.
fs::path prepare_home() {
    const fs::path home = fs::temp_directory_path() /
                          ("blaise-ui-" + std::to_string(getpid()));
    fs::remove_all(home);
    const std::string head =
        "From: Ana Novak <ana@x.org>\r\nTo: Me <me@x.org>\r\n"
        "Date: Thu, 13 Feb 1975 23:31:30 +0000\r\n";
    util::write_file(home / "Mail/inbox/1",
                     head + "Subject: First\r\nMessage-ID: <1@x>\r\n\r\n"
                            "Hello from http://x.org/ again.\r\n> a quote\r\n"
                            "-- \r\nAna\r\n");
    util::write_file(home / "Mail/inbox/2",
                     head + "Subject: Re: First\r\nMessage-ID: <2@x>\r\n"
                            "In-Reply-To: <1@x>\r\n\r\nThe reply.\r\n");
    util::write_file(home / "Mail/inbox/3",
                     head + "Subject: Report\r\nMessage-ID: <3@x>\r\n"
                            "Content-Type: multipart/mixed; boundary=B\r\n\r\n"
                            "--B\r\nContent-Type: text/plain\r\n\r\nSee "
                            "attached.\r\n--B\r\nContent-Type: text/plain\r\n"
                            "Content-Disposition: attachment; filename=a.txt"
                            "\r\n\r\nfile\r\n--B--\r\n");
    util::write_file(home / "cfg/accountrc",
                     "[Account: 1]\naccount_name=Main\nname=Me\n"
                     "address=me@x.org\nprotocol=0\nreceive_server=pop\n"
                     "smtp_server=smtp\nuser_id=me\nis_default=1\n"
                     "ssl_pop=0\nssl_smtp=0\nset_smtpport=1\nsmtp_port=25\n"
                     "set_popport=1\npop_port=110\n");
    return home;
}

} // namespace

// Runs the scenario against one window.
class desktop_test {
public:
    desktop_test(ui::main_window &window, fs::path home,
                 testing::fake_smtp &smtp, testing::fake_pop3 &pop)
        : window_(window), home_(std::move(home)), smtp_(smtp), pop_(pop) {}

    // Advance by one tick; returns false at the end.
    bool tick() {
        if (next_ >= steps_.size())
            return false;
        step &current = steps_[next_];
        if (current.ready && !current.ready()) {
            if (++waited_ > 1000) {
                std::cerr << "timed out at: " << current.name << ", status: "
                          << window_.message_ << '\n';
                CHECK(false);
                next_ = steps_.size();
                window_.request_close();
                return false;
            }
            return true;
        }
        waited_ = 0;
        ++next_;
        if (std::getenv("BLAISE_TEST_TRACE") != nullptr)
            std::cerr << "step: " << current.name << std::endl;
        if (current.run)
            current.run();
        return true;
    }

    // Build the scenario.
    void plan() {
        test_chrome();
        test_reading();
        test_threads_and_search();
        test_flags();
        test_compose_and_send();
        test_receive();
        test_folders();
        test_settings();
        test_every_command();
    }

private:
    // One step: wait for a condition, then act.
    struct step {
        const char *name;
        std::function<bool()> ready;
        std::function<void()> run;
    };

    void add(const char *name, std::function<bool()> ready,
             std::function<void()> run) {
        steps_.push_back({name, std::move(ready), std::move(run)});
    }

    void act(const char *name, std::function<void()> run) {
        add(name, nullptr, std::move(run));
    }

    void wait(const char *name, std::function<bool()> ready) {
        add(name, std::move(ready), nullptr);
    }

    // Report whether the engine has nothing left to do.
    void settle(const char *name) {
        auto done = std::make_shared<bool>(false);
        act(name, [this, done] {
            window_.jobs_.run([this, done] {
                window_.jobs_.deliver([done] { *done = true; });
            });
        });
        wait(name, [done] { return *done; });
    }

    void select_folder(const std::string &id) {
        window_.folders_.select_id(id);
        window_.open_selected_folder();
    }

    void click_row(std::uint32_t number) {
        window_.summary_.select_number(number);
        window_.show_selection();
    }

    std::size_t node_total(const std::string &id) {
        const app::folder_node *node = window_.folders_.find_node(id);
        return node == nullptr ? 0
                               : static_cast<std::size_t>(node->total_count);
    }

    void test_chrome() {
        act("chrome", [this] {
            for (const char *name : {"fileNew", "getMail", "reply", "forward",
                                     "deleteMail", "addressBook"})
                CHECK(ui::tool_icon(name) != nullptr);
            CHECK(ui::tool_icon("noSuchTool") == nullptr);
            // The suite's shell: two strips, four-part status, same size.
            CHECK(window_.toolbar_.get_items().size() > 10);
            CHECK(window_.search_bar_.get_items().size() == 4);
            CHECK(window_.status_.get_parts().size() == 4);
            CHECK(window_.status_.get_parts()[0].width == 130);
            CHECK(window_.status_.get_parts()[1].width == 150);
            CHECK(window_.status_.get_parts()[2].width == 70);
            // Backends take their menu bar out of this, so only the width
            // is the same everywhere.
            CHECK(window_.get_bounds().d.w == 1000);
            const auto &tops = window_.menu.tops();
            CHECK(tops.size() == 6);
            CHECK(tops.front().title == "File");
            CHECK(tops.back().title == "Help");
        });
        wait("tree", [this] {
            return window_.folders_.find_node("#mh/Mailbox/inbox") != nullptr;
        });
    }

    void test_reading() {
        act("open inbox", [this] { select_folder("#mh/Mailbox/inbox"); });
        wait("inbox loaded",
             [this] { return window_.model_.rows().size() == 3; });
        act("title", [this] {
            CHECK(window_.get_title() == "Inbox - Blaise");
            CHECK(window_.status_.get_parts()[1].text.starts_with(
                "3 unread in Inbox"));
            CHECK(window_.folders_.find_node("#mh/Mailbox/inbox")
                      ->unread_count == 3);
            click_row(1);
        });
        wait("message shown", [this] {
            return window_.message_pane_.get_text().find("Hello from") !=
                   std::string::npos;
        });
        settle("flag saved");
        act("read flag", [this] {
            const auto row = window_.model_.row_of(1);
            CHECK(row.has_value());
            CHECK(row && !window_.model_.at(*row).has(mail::flag_unread));
            CHECK(window_.message_pane_.document().links.size() == 1);
            CHECK(window_.folders_.find_node("#mh/Mailbox/inbox")
                      ->unread_count == 2);
            CHECK(window_.message_pane_.find_next("quote"));
            CHECK(!window_.message_pane_.find_next("zebra"));
            click_row(3);
        });
        wait("attachment shown", [this] {
            return window_.message_pane_.document().attachments.size() == 1;
        });
        act("attachment", [this] {
            CHECK(window_.message_pane_.get_text().find("[Attachment: a.txt") !=
                  std::string::npos);
            window_.full_headers_ = false;
            window_.run("toggleHeaders");
            CHECK(window_.message_pane_.get_text().find("Message-ID") !=
                  std::string::npos);
            window_.run("toggleHeaders");
        });
    }

    void test_threads_and_search() {
        act("threads", [this] {
            CHECK(window_.model_.threaded());
            window_.run("threadView");
            CHECK(!window_.model_.threaded());
            window_.run("threadView");
            const auto row = window_.model_.row_of(2);
            CHECK(row && window_.model_.rows()[*row].depth == 1);
            window_.search_box_.set_text("report");
            window_.apply_quick_search();
            CHECK(window_.model_.rows().size() == 1);
            window_.search_box_.set_text("");
            window_.apply_quick_search();
            CHECK(window_.model_.rows().size() == 3);
            window_.show_box_.set_selected_index(1);
            window_.apply_view_filter();
            CHECK(window_.model_.rows().size() == 1);
            window_.show_box_.set_selected_index(0);
            window_.apply_view_filter();
            window_.set_sort(mail::sort_key::subject);
            CHECK(window_.model_.sort() == mail::sort_key::subject);
            window_.set_sort(mail::sort_key::date);
            // The body search runs on the worker.
            window_.search_box_.set_text("b attached");
            window_.apply_quick_search();
        });
        wait("body search",
             [this] { return window_.model_.rows().size() == 1; });
        act("clear search", [this] {
            CHECK(window_.model_.at(0).subject == "Report");
            window_.search_box_.set_text("");
            window_.apply_quick_search();
            CHECK(window_.model_.rows().size() == 3);
        });
    }

    void test_flags() {
        act("mark", [this] {
            click_row(2);
            window_.run("markMessage");
            CHECK(window_.model_.at(*window_.model_.row_of(2))
                      .has(mail::flag_marked));
            window_.run("markUnread");
            CHECK(window_.model_.at(*window_.model_.row_of(2))
                      .has(mail::flag_unread));
            window_.summary_.select_number(1);
            window_.run("nextUnread");
            CHECK(window_.summary_.selected_numbers().front() == 2);
        });
        settle("flags stored");
    }

    void test_compose_and_send() {
        act("reply", [this] {
            click_row(1);
        });
        wait("first shown", [this] {
            return window_.message_pane_.get_text().find("Hello from") !=
                   std::string::npos;
        });
        act("open reply", [this] {
            window_.run("reply");
            CHECK(window_.composers_.size() == 1);
            ui::compose_window &compose = *window_.composers_.front();
            CHECK(compose.get_created());
            const compose::draft text = compose.current();
            CHECK(text.to == "Ana Novak <ana@x.org>");
            CHECK(text.subject == "Re: First");
            CHECK(compose.body_.get_text().find("> Hello from") !=
                  std::string::npos);
            compose.body_.insert(0, "Thanks.\n");
            compose.send();
        });
        wait("composer closed",
             [this] { return window_.composers_.empty(); });
        wait("sent", [this] { return !smtp_.message.empty(); });
        wait("filed", [this] { return node_total("#mh/Mailbox/sent") == 1; });
        settle("source marked");
        act("after send", [this] {
            CHECK(smtp_.sender == "me@x.org");
            CHECK(smtp_.recipients ==
                  (std::vector<std::string>{"ana@x.org"}));
            const auto row = window_.model_.row_of(1);
            CHECK(row && window_.model_.at(*row).has(mail::flag_replied));
            // Unsent work is not lost: closing saves a draft.
            window_.run("fileNew");
            CHECK(window_.composers_.size() == 1);
            ui::compose_window &compose = *window_.composers_.front();
            compose.to_.set_text("later@x.org");
            compose.subject_.set_text("Unfinished");
            compose.save();
            compose.finish();
        });
        wait("draft saved",
             [this] { return node_total("#mh/Mailbox/draft") == 1; });
        wait("composer gone",
             [this] { return window_.composers_.empty(); });
        act("open draft", [this] {
            select_folder("#mh/Mailbox/draft");
        });
        wait("draft listed",
             [this] { return window_.model_.rows().size() == 1; });
        act("pick draft", [this] {
            click_row(window_.model_.at(0).number);
        });
        wait("draft shown", [this] {
            return window_.message_pane_.get_text().find("Unfinished") !=
                   std::string::npos;
        });
        act("reopen", [this] {
            window_.run("editMessage");
            CHECK(window_.composers_.size() == 1);
            CHECK(window_.composers_.front()->current().to == "later@x.org");
            window_.composers_.front()->finish();
        });
        wait("reopened closed",
             [this] { return window_.composers_.empty(); });
    }

    void test_receive() {
        act("receive", [this] {
            select_folder("#mh/Mailbox/inbox");
            window_.run("getMail");
        });
        wait("received", [this] {
            return window_.message_.find("new message") != std::string::npos;
        });
        wait("listed", [this] { return window_.model_.rows().size() == 4; });
        act("delete", [this] {
            CHECK(pop_.committed);
            click_row(3);
            window_.run("deleteMail");
        });
        wait("deleted", [this] { return window_.model_.rows().size() == 3; });
        wait("in trash",
             [this] { return node_total("#mh/Mailbox/trash") == 1; });
    }

    void test_folders() {
        act("new folder", [this] {
            window_.core_.create_folder("#mh/Mailbox", "work");
        });
        wait("folder made", [this] {
            return window_.folders_.find_node("#mh/Mailbox/work") != nullptr;
        });
        act("move", [this] {
            window_.core_.transfer("#mh/Mailbox/inbox", {1},
                                   "#mh/Mailbox/work", false);
        });
        wait("moved", [this] { return window_.model_.rows().size() == 2; });
        act("choices", [this] {
            const auto choices = window_.folder_choices();
            CHECK(choices.size() >= 6);
            CHECK(std::ranges::any_of(choices, [](const auto &entry) {
                return entry.second == "#mh/Mailbox/work";
            }));
        });
    }

    void test_settings() {
        act("accounts", [this] {
            ui::account_window &dialog = window_.account_window_;
            window_.run("accounts");
            CHECK(dialog.rows_.get_items().size() == 1);
            CHECK(dialog.address_.get_text() == "me@x.org");
            CHECK(dialog.protocol_.get_selected_index() == 1);
            dialog.password_.on_native_text_input("s3cret");
            dialog.add_account();
            CHECK(dialog.rows_.get_items().size() == 2);
            dialog.address_.set_text("two@x.org");
            dialog.remove_account();
            CHECK(dialog.rows_.get_items().size() == 1);
            CHECK(dialog.accept());
            dialog.close(native::dialog_result::cancelled);
        });
        settle("accounts stored");
        wait("password kept", [this] {
            return window_.core_.accounts().password(1, false) == "s3cret";
        });
        act("preferences", [this] {
            ui::preferences_window &dialog = window_.preferences_window_;
            window_.run("preferences");
            CHECK(dialog.wrap_.get_text() == "72");
            dialog.wrap_.set_text("66");
            CHECK(dialog.accept());
            dialog.close(native::dialog_result::cancelled);
        });
        wait("preferences kept", [this] {
            return window_.core_.preferences().wrap_length == 66;
        });
        act("filters", [this] {
            ui::filter_window &dialog = window_.filter_window_;
            window_.run("filters");
            dialog.text_.set_text("subject matchcase \"x\" mark");
            dialog.add_rule();
            CHECK(dialog.rows_.get_items().size() == 1);
            CHECK(dialog.accept());
            dialog.close(native::dialog_result::cancelled);
        });
        wait("filters kept",
             [this] { return window_.core_.rules().size() == 1; });
        act("address book", [this] {
            ui::address_window &dialog = window_.address_window_;
            window_.run("addressBook");
            dialog.name_.set_text("Ana Novak");
            dialog.email_.set_text("ana@x.org");
            dialog.add_person();
            CHECK(dialog.rows_.get_items().size() == 1);
            CHECK(fs::exists(home_ / "cfg/addrbook/addrbook--index.xml"));
            CHECK(window_.books_.complete("ana").size() == 1);
            dialog.destroy();
            window_.run("networkLog");
            CHECK(window_.log_window_.get_created());
            window_.log_window_.destroy();
        });
    }

    void test_every_command() {
        act("commands", [this] {
            // Everything that opens no modal surface runs without harm.
            for (const char *command :
                 {"refreshFolder", "previousUnread", "nextUnread", "findNext",
                  "markRead", "lockMessage", "unlockMessage", "selectAll",
                  "copy", "applyFilters", "emptyTrash", "sendMail"})
                window_.run(command);
        });
        settle("commands done");
        act("finish", [this] {
            CHECK(window_.status_.get_parts().size() == 4);
            window_.request_close();
        });
    }

    ui::main_window &window_;
    fs::path home_;
    testing::fake_smtp &smtp_;
    testing::fake_pop3 &pop_;
    std::vector<step> steps_;
    std::size_t next_ = 0;
    int waited_ = 0;
};

int program(int, char **) {
    const fs::path home = prepare_home();
    testing::fake_smtp smtp;
    testing::fake_pop3 pop({"From: b@x.org\r\nSubject: Fresh\r\n"
                            "Message-ID: <9@x>\r\n\r\nNew.\r\n"});
    pop.password.clear();
    app::options settings;
    settings.where = {home / "cfg", home / "Mail"};
    settings.connect = [&](const net::endpoint &target)
        -> std::unique_ptr<net::transport> {
        if (target.port == 110)
            return std::make_unique<testing::borrowed_transport>(pop);
        return std::make_unique<testing::borrowed_transport>(smtp);
    };
    int status = 0;
    {
        ui::main_window window(settings);
        desktop_test test(window, home, smtp, pop);
        test.plan();
        std::jthread driver;
        window.on_wnd_create.connect([&] {
            driver = std::jthread([&](std::stop_token stop) {
                std::atomic<bool> busy{false};
                while (!stop.stop_requested()) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(5));
                    if (busy.exchange(true))
                        continue;
                    native::app::post([&] {
                        test.tick();
                        busy = false;
                    });
                }
            });
            return false;
        });
        status = native::app::run(window);
        driver.request_stop();
        driver.join();
    }
    fs::remove_all(home);
    return status ? status : testing::summary("desktop");
}
