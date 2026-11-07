//
// Summaries, Claws' cache and mark files, the MH store, mbox and
// folderlist.xml, plus configuration files.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <filesystem>
#include <unistd.h>

#include "check.h"
#include "mail/folder_list.h"
#include "mail/mbox.h"
#include "mail/mh_folder.h"
#include "prefs/account.h"
#include "prefs/common.h"
#include "util/files.h"
#include "util/text.h"

namespace fs = std::filesystem;
using namespace mail;

namespace {

// A directory removed at the end of its scope.
struct scratch_directory {
    fs::path path;

    scratch_directory() {
        path = fs::temp_directory_path() /
               ("blaise-store-" + std::to_string(getpid()));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~scratch_directory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string sample(const std::string &subject, const std::string &id) {
    return "From: =?UTF-8?Q?Ana_=C4=8C?= <ana@x.org>\r\n"
           "To: me@x.org\r\nSubject: " +
           subject + "\r\nMessage-ID: <" + id +
           ">\r\nDate: Thu, 13 Feb 1975 23:31:30 +0000\r\n"
           "References: <r1> <r2>\r\n\r\nbody\r\n";
}

void test_summary() {
    const msg_info info = parse_msg_info(sample("Hi", "m1@x"), 7);
    CHECK(info.number == 7);
    CHECK(info.subject == "Hi");
    CHECK(info.from_name == "Ana \xC4\x8C");
    CHECK(info.message_id == "m1@x");
    CHECK(info.references.size() == 2);
    CHECK(info.in_reply_to == "r2");
    CHECK(info.date_time == 161566290);
    CHECK(strip_subject("Re: [list] Fwd: Re[2]: hello") == "hello");
    CHECK(strip_subject("[x]") == "[x]");
    msg_info label;
    for (int i = 0; i < 16; ++i) {
        label.set_color_label(i);
        CHECK(label.color_label() == i);
    }
}

void test_mh(const fs::path &base) {
    const fs::path root = base / "Mail";
    {
        mh_folder store("Mailbox", root);
        folder_item *inbox = store.find_special(item_type::inbox);
        CHECK(inbox != nullptr);
        CHECK(store.find_special(item_type::trash) != nullptr);
        CHECK(inbox->identifier() == "#mh/Mailbox/inbox");
        CHECK(store.add(*inbox, sample("one", "1@x"), 0) == 1);
        CHECK(store.add(*inbox, sample("two", "2@x"), 0) == 2);
        CHECK(util::write_file(root / "inbox/note.txt", "not mail"));
        inbox->scan();
        CHECK(inbox->total_count == 2);
        CHECK(inbox->unread_count == 2);
        inbox->change_flags(1, flag_marked, flag_new | flag_unread);
        CHECK(inbox->unread_count == 1);
        inbox->save();
    }
    {
        // Reopening keeps flags, through the same files Claws reads.
        mh_folder store("Mailbox", root);
        folder_item *inbox = store.find_special(item_type::inbox);
        CHECK(inbox->messages().size() == 2);
        const msg_info *first = inbox->find(1);
        CHECK(first != nullptr && first->has(flag_marked));
        CHECK(first != nullptr && !first->has(flag_unread));
        CHECK(first != nullptr && first->subject == "one");
        CHECK(inbox->find(2)->has(flag_unread));

        // A deleted file disappears on the next scan.
        fs::remove(root / "inbox/2");
        inbox->scan();
        CHECK(inbox->total_count == 1);

        folder_item *trash = store.find_special(item_type::trash);
        CHECK(transfer_messages(*inbox, *trash, {1}, false) == 1);
        CHECK(inbox->total_count == 0);
        CHECK(trash->total_count == 1);
        CHECK(trash->messages().begin()->second.has(flag_marked));
        CHECK(!fs::exists(root / "inbox/1"));

        folder_item *work = store.create_folder(store.root(), "work");
        CHECK(work != nullptr);
        CHECK(store.create_folder(store.root(), "work") == nullptr);
        CHECK(store.create_folder(store.root(), "a/b") == nullptr);
        CHECK(store.create_folder(store.root(), "..") == nullptr);
        folder_item *sub = store.create_folder(*work, "sub");
        CHECK(sub != nullptr && sub->path == "work/sub");
        CHECK(store.rename_folder(*work, "job"));
        CHECK(sub->path == "job/sub");
        CHECK(fs::exists(root / "job/sub"));
        CHECK(store.find_item("job/sub") == sub);
        CHECK(store.remove_folder(*work));
        CHECK(!fs::exists(root / "job"));
    }
}

void test_corrupt_cache(const fs::path &base) {
    const fs::path root = base / "Corrupt";
    mh_folder store("Corrupt", root);
    folder_item *inbox = store.find_special(item_type::inbox);
    store.add(*inbox, sample("one", "1@x"), 0);
    inbox->scan();
    inbox->save();
    std::string cache = *util::read_file(root / "inbox/.claws_cache");
    cache.resize(cache.size() / 2);
    util::write_file(root / "inbox/.claws_cache", cache);
    CHECK(read_msg_cache(root / "inbox").empty());
    util::write_file(root / "inbox/.claws_cache", "\x18");
    CHECK(read_msg_cache(root / "inbox").empty());
    mh_folder again("Corrupt", root);
    folder_item *rebuilt = again.find_special(item_type::inbox);
    CHECK(rebuilt->messages().size() == 1);
}

// These files were written by a real Claws Mail 4 run under Xvfb.
void test_real_claws_files(const fs::path &base) {
    const fs::path source = FIXTURES "/claws";
    const fs::path folder = base / "RealClaws/inbox";
    fs::create_directories(folder);
    fs::copy_file(source / "claws_cache", folder / ".claws_cache");
    fs::copy_file(source / "claws_mark", folder / ".claws_mark");
    const msg_map messages = read_msg_cache(folder);
    CHECK(messages.size() == 3);
    const msg_info &first = messages.at(1);
    CHECK(first.subject == "Claws message 1");
    CHECK(first.from == "Ana \xC4\x8C <ana@x.org>");
    CHECK(first.from_name == "Ana \xC4\x8C");
    CHECK(first.to == "me@x.org");
    CHECK(first.message_id == "c1@x");
    CHECK(first.date == "Sat, 07 Nov 2026 10:00:01 +0000");
    CHECK(first.size == 145);
    CHECK(first.has(flag_unread));
    CHECK(!first.has(flag_new));

    // What Blaise writes is byte for byte what Claws wrote.
    CHECK(write_msg_cache(folder, messages));
    CHECK(*util::read_file(folder / ".claws_cache") ==
          *util::read_file(source / "claws_cache"));
    CHECK(*util::read_file(folder / ".claws_mark") ==
          *util::read_file(source / "claws_mark"));

    // Claws' own folderlist.xml and accountrc load.
    std::string list = *util::read_file(source / "folderlist.xml");
    list = util::replace_all(list, "FIXTURE_MAIL",
                        (base / "RealClaws").string());
    CHECK(util::write_file(base / "real-folderlist.xml", list));
    folder_list folders;
    CHECK(folders.load(base / "real-folderlist.xml", nullptr));
    CHECK(folders.find("#mh/Mailbox/inbox") != nullptr);
    CHECK(folders.find("#mh/Mailbox/inbox")->type == item_type::inbox);
    fs::create_directories(base / "real-cfg");
    fs::copy_file(source / "accountrc", base / "real-cfg/accountrc");
    prefs::account_list accounts;
    accounts.load(base / "real-cfg");
    CHECK(accounts.all().size() == 1);
    CHECK(accounts.all()[0].address == "me@x.org");
    CHECK(accounts.all()[0].kind == prefs::protocol::none);
}

void test_mbox() {
    std::string file;
    append_mbox(file, "Subject: a\r\n\r\nFrom the top\r\n>From x\r\n",
                "a@x.org", 161566290);
    append_mbox(file, "Subject: b\n\nbody\n", "", 161566290);
    CHECK(file.starts_with("From a@x.org Thu Feb 13 23:31:30 1975\n"));
    CHECK(file.find("\n>From the top\n") != std::string::npos);
    const auto messages = split_mbox(file);
    CHECK(messages.size() == 2);
    CHECK(messages[0] == "Subject: a\n\nFrom the top\n>From x\n");
    CHECK(messages[1] == "Subject: b\n\nbody\n");
    CHECK(split_mbox("").empty());
    CHECK(split_mbox("no delimiter\n").empty());
}

void test_folder_list(const fs::path &base) {
    const fs::path rc = base / "cfg/folderlist.xml";
    {
        folder_list list;
        auto &store = list.add(
            std::make_unique<mh_folder>("Mailbox", base / "ListMail"));
        folder_item *work = store.create_folder(store.root(), "work");
        work->sort = sort_key::subject;
        work->sort_ascending = false;
        CHECK(list.save(rc));
    }
    folder_list list;
    CHECK(list.load(rc, nullptr));
    CHECK(list.all().size() == 1);
    folder_item *work = list.find("#mh/Mailbox/work");
    CHECK(work != nullptr);
    CHECK(work != nullptr && work->sort == sort_key::subject);
    CHECK(work != nullptr && !work->sort_ascending);
    CHECK(list.find("#mh/Mailbox/inbox")->type == item_type::inbox);
    CHECK(list.find("#mh/Other/inbox") == nullptr);
    CHECK(list.special_for(work, item_type::trash) != nullptr);
    folder_list missing;
    CHECK(!missing.load(base / "absent.xml", nullptr));
}

void test_rc_and_accounts(const fs::path &base) {
    auto file = prefs::rc_file::parse(
        "# comment\n[Account: 1]\nname=Me\nunknown=keep\nsig=a\\nb\n\n"
        "[Account: 3]\naddress=x@y\n");
    CHECK(file.sections().size() == 2);
    CHECK(file.find("Account: 1")->get("sig") == "a\nb");
    CHECK(file.find("Account: 1")->get_int("absent", 5) == 5);
    const auto again = prefs::rc_file::parse(file.serialize());
    CHECK(again.find("Account: 1")->get("sig") == "a\nb");

    const fs::path dir = base / "cfg";
    util::write_file(dir / "accountrc",
                     "[Account: 2]\naccount_name=Work\nname=Ana\n"
                     "address=ana@x.org\nprotocol=1\nreceive_server=i.x.org\n"
                     "smtp_server=s.x.org\nuser_id=ana\nssl_imap=1\n"
                     "ssl_smtp=2\nset_sent_folder=1\n"
                     "sent_folder=#imap/w/Sent\nclaws_only=42\n");
    prefs::account_list accounts;
    accounts.load(dir);
    CHECK(accounts.all().size() == 1);
    const prefs::account *work = accounts.find(2);
    CHECK(work != nullptr);
    CHECK(work->effective_receive_port() == 993);
    CHECK(work->effective_smtp_port() == 587);
    CHECK(work->sent_folder == "#imap/w/Sent");
    CHECK(work->from_header() == "Ana <ana@x.org>");
    CHECK(accounts.default_account() == work);
    accounts.set_password(2, false, "sec\nret");
    int added = accounts.add({});
    CHECK(added == 3);
    accounts.set_default(3);
    CHECK(accounts.default_account()->id == 3);
    accounts.remove(3);
    CHECK(accounts.save(dir));
    prefs::account_list loaded;
    loaded.load(dir);
    CHECK(loaded.password(2, false) == "sec\nret");
    CHECK(loaded.find(3) == nullptr);
    // Settings Blaise does not model survive.
    CHECK(util::read_file(dir / "accountrc")->find("claws_only=42") !=
          std::string::npos);
    CHECK((fs::status(dir / "passwordrc").permissions() &
           fs::perms::group_read) == fs::perms::none);

    prefs::common values;
    values.wrap_length = 66;
    values.quote_mark = "| ";
    CHECK(values.save(dir));
    prefs::common read_back;
    read_back.load(dir);
    CHECK(read_back.wrap_length == 66);
    CHECK(read_back.quote_mark == "| ");
}

} // namespace

int main() {
    const scratch_directory scratch;
    test_summary();
    test_mh(scratch.path);
    test_corrupt_cache(scratch.path);
    test_real_claws_files(scratch.path);
    test_mbox();
    test_folder_list(scratch.path);
    test_rc_and_accounts(scratch.path);
    return testing::summary("store");
}
