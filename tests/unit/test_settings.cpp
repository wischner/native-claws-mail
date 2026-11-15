// Storage regressions: real process restarts, XDG migration and failed writes.
// GPL 3.0 or later (see: LICENSE)
#include <cstdlib>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

#include "app/engine.h"
#include "addr/book.h"
#include "check.h"
#include "compose/draft.h"
#include "compose/queue.h"
#include "util/files.h"
#include "util/log.h"

namespace fs = std::filesystem;

struct listener : app::engine_listener {
    int errors = 0;
    int notifications = 0;
    app::folder_view view;
    void folders_changed(const std::vector<app::folder_node> &) override {}
    void folder_loaded(const app::folder_view &value) override { view = value; }
    void status(const std::string &) override {}
    void failure(const std::string &) override { ++errors; }
    void mail_received(int) override {}
    void preferences_changed(const prefs::common &, bool) override { ++notifications; }
};

int reload(const fs::path &root) {
    app::inline_executor jobs;
    listener observed;
    app::engine engine({root / "config", root / "Mail"}, jobs, observed);
    engine.load();
    CHECK(observed.errors == 0);
    const auto values = engine.preferences();
    CHECK(values.auto_check && values.check_minutes == 17 && values.check_on_startup);
    CHECK(!values.auto_wrap && values.wrap_length == 66);
    CHECK(values.quote_mark == "| " && !values.save_message_to_sent);
    CHECK(!values.mark_read_on_open && !values.threaded_by_default);
    CHECK(values.empty_trash_on_exit);
    const auto accounts = engine.accounts();
    CHECK(accounts.all().size() == 1);
    CHECK(accounts.password(1, false) == "receive secret");
    CHECK(accounts.password(1, true) == "send secret");
    CHECK(accounts.default_account()->signature_text == "first\nsecond\\line");
    CHECK(accounts.default_account()->effective_smtp_port() == 2525);
    CHECK(engine.rules().size() == 1);
    engine.open_folder("#mh/Mailbox/work");
    CHECK(!observed.view.threaded && observed.view.sort == mail::sort_key::subject);
    CHECK(!observed.view.sort_ascending);
    engine.create_folder("#mh/Mailbox", "after-restart");
    engine.open_folder("#mh/Mailbox/after-restart");
    CHECK(!observed.view.threaded);
    addr::book_list books;
    books.load(root / "config/addrbook");
    CHECK(books.complete("ana").size() == 1);
    const auto reply = compose::reply_to("From: Ana <ana@example.org>\r\nSubject: Test\r\n\r\nBody\r\n",
        *accounts.default_account(), values, compose::reply_mode::sender);
    CHECK(reply.body.find("| Body") != std::string::npos);
    auto draft = compose::new_draft(*accounts.default_account());
    draft.to = "ana@example.org";
    draft.subject = "Persisted behavior";
    draft.body = std::string(120, 'x');
    const auto outgoing = compose::to_outgoing(draft, *accounts.default_account(), values, 0);
    CHECK(outgoing && outgoing->body == draft.body); // Saved auto-wrap=false is applied.
    engine.queue(draft);
    engine.open_folder("#mh/Mailbox/queue");
    CHECK(observed.view.messages.size() == 1);
    const auto number = observed.view.messages.front().number;
    engine.fetch_message("#mh/Mailbox/queue", number, [](const auto &raw) {
        CHECK(raw && compose::read_queue_entry(*raw).sent_folder.empty());
    });
    engine.transfer("#mh/Mailbox/queue", {number}, "#mh/Mailbox/trash", false);
    engine.open_folder("#mh/Mailbox/trash");
    CHECK(observed.view.messages.size() == 1);
    engine.shut_down();
    engine.open_folder("#mh/Mailbox/trash");
    CHECK(observed.view.messages.empty()); // Saved empty-trash-on-exit is applied.
    CHECK(observed.errors == 0);
    return testing::summary("settings restart");
}

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--reload") return reload(argv[2]);
    const auto root = fs::temp_directory_path() / ("blaise-settings-" + std::to_string(getpid()));
    fs::remove_all(root);
    app::inline_executor jobs;
    listener observed;
    app::paths paths{root / "config", root / "Mail"};
    app::engine engine(paths, jobs, observed);
    engine.load();
    CHECK(observed.errors == 0 && observed.notifications == 1);
    auto values = engine.preferences();
    values.auto_check = true;
    values.check_minutes = 17;
    values.check_on_startup = true;
    values.auto_wrap = false;
    values.wrap_length = 66;
    values.quote_mark = "| ";
    values.save_message_to_sent = false;
    values.mark_read_on_open = false;
    values.threaded_by_default = false;
    values.empty_trash_on_exit = true;
    engine.set_preferences(values);
    CHECK(observed.notifications == 2);
    prefs::account_list accounts;
    prefs::account account;
    account.address = "me@example.org";
    account.kind = prefs::protocol::none;
    account.signature_text = "first\nsecond\\line";
    account.smtp_port = 2525;
    const int id = accounts.add(account);
    accounts.set_password(id, false, "receive secret");
    accounts.set_password(id, true, "send secret");
    engine.set_accounts(accounts);
    engine.set_rules({*filter::parse_rule("subject matchcase \"report\" mark")});
    engine.create_folder("#mh/Mailbox", "work");
    engine.set_folder_view("#mh/Mailbox/work", mail::sort_key::subject, false, false);
    addr::book_list books;
    addr::person person;
    person.common_name = "Ana";
    person.emails.push_back({"", "", "ana@example.org", ""});
    books.primary().add(person);
    CHECK(books.save(paths.address_book()));
    // No shutdown flush: every Save must already be on disk.
    const auto child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        execl(argv[0], argv[0], "--reload", root.c_str(), nullptr);
        _exit(127);
    }
    int status = 0;
    if (child > 0) {
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK((fs::status(paths.config).permissions() & fs::perms::all) == fs::perms::owner_all);
    CHECK((fs::status(paths.config / "passwordrc").permissions() & fs::perms::all) ==
          (fs::perms::owner_read | fs::perms::owner_write));
    const auto before = util::read_file(paths.config / "blaiserc");
    fs::permissions(paths.config, fs::perms::owner_read | fs::perms::owner_exec);
    values.wrap_length = 90;
    engine.set_preferences(values);
    CHECK(observed.errors == 1);
    CHECK(observed.notifications == 2);
    CHECK(engine.preferences().wrap_length == 66);
    CHECK(util::read_file(paths.config / "blaiserc") == before);
    engine.set_rules({});
    CHECK(observed.errors == 2 && engine.rules().size() == 1);
    engine.set_accounts({});
    CHECK(observed.errors == 3 && engine.accounts().all().size() == 1);
    fs::permissions(paths.config, fs::perms::owner_all);
    // A companion-file failure restores the previous account file.
    const auto old_accounts = util::read_file(paths.config / "accountrc");
    fs::rename(paths.config / "passwordrc", paths.config / "password.backup");
    fs::create_directory(paths.config / "passwordrc");
    CHECK(!accounts.save(paths.config));
    CHECK(util::read_file(paths.config / "accountrc") == old_accounts);
    fs::remove(paths.config / "passwordrc");
    fs::rename(paths.config / "password.backup", paths.config / "passwordrc");
    // An unreadable existing configuration must not become an empty default file.
    fs::permissions(paths.config / "blaiserc", fs::perms::none);
    engine.set_preferences(values);
    CHECK(observed.errors == 4);
    fs::permissions(paths.config / "blaiserc", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(util::read_file(paths.config / "blaiserc") == before);
    const auto index = paths.address_book() / "addrbook--index.xml";
    const auto old_book = util::read_file(paths.address_book() / "addrbook-000001.xml");
    fs::rename(index, paths.address_book() / "index.backup");
    fs::create_directory(index);
    books.primary().people().front().common_name = "changed";
    CHECK(!books.save(paths.address_book()));
    CHECK(util::read_file(paths.address_book() / "addrbook-000001.xml") == old_book);
    fs::remove(index);
    fs::rename(paths.address_book() / "index.backup", index);
    // A load failure must not be followed by a refresh/shutdown that overwrites the bad file.
    const auto broken = root / "broken";
    CHECK(util::write_file(broken / "folderlist.xml", "<folderlist>"));
    listener failed;
    app::engine invalid_engine({broken, root / "other-mail"}, jobs, failed);
    invalid_engine.load();
    invalid_engine.refresh_tree();
    invalid_engine.shut_down();
    CHECK(failed.errors == 1);
    CHECK(*util::read_file(broken / "folderlist.xml") == "<folderlist>");
    // A failed directory creation leaves the existing destination untouched.
    CHECK(util::write_file(root / "target", "old"));
    CHECK(!util::write_file(root / "target/child", "bad"));
    CHECK(*util::read_file(root / "target") == "old");
    prefs::rc_file invalid = prefs::rc_file::parse("[Common]\nautochk_interval=-1\nlinewrap_length=999999999999\n");
    CHECK(invalid.save(root / "invalid/blaiserc"));
    prefs::common defaults;
    defaults.load(root / "invalid");
    CHECK(defaults.check_minutes == 10 && defaults.wrap_length == 72);

    // Isolated XDG profile: migrate settings and data, leave source untouched.
    const auto home = root / "home";
    setenv("HOME", home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "xdg-config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "xdg-data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (root / "xdg-cache").c_str(), 1);
    CHECK(util::write_file(home / ".blaise/blaiserc", *before, true));
    CHECK(books.save(home / ".blaise/addrbook"));
    auto xdg = app::paths::defaults();
    CHECK(xdg.config == root / "xdg-config/blaise");
    CHECK(xdg.mail == root / "xdg-data/blaise/Mail");
    CHECK(xdg.imap_cache() == root / "xdg-cache/blaise/imapcache");
    xdg.prepare();
    CHECK(util::read_file(xdg.config / "blaiserc") == before);
    CHECK(fs::exists(home / ".blaise/blaiserc"));
    books.load(xdg.address_book());
    CHECK(books.complete("ana").size() == 1);
    CHECK(util::write_file(xdg.config / "blaiserc", "[Common]\nlinewrap_length=88\n", true));
    xdg.prepare();
    defaults.load(xdg.config);
    CHECK(defaults.wrap_length == 88); // Never overwrite a newer profile.
    fs::create_directory(home / "Mail");
    CHECK(app::paths::defaults().mail == home / "Mail");
    setenv("XDG_CONFIG_HOME", "relative-is-invalid", 1);
    setenv("XDG_DATA_HOME", "", 1);
    CHECK(app::paths::defaults().config == home / ".config/blaise");
    CHECK(app::paths::defaults().data == home / ".local/share/blaise");
    fs::remove_all(root);
    return testing::summary("settings");
}
