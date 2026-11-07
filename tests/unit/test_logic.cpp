//
// Filter conditions and rules, the address book, composing, the queue
// format, the message list and message layout.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <filesystem>
#include <unistd.h>

#include "addr/book.h"
#include "check.h"
#include "compose/draft.h"
#include "compose/queue.h"
#include "filter/matcher.h"
#include "filter/rules.h"
#include "mail/mh_folder.h"
#include "mime/part.h"
#include "util/files.h"
#include "view/document.h"
#include "view/summary.h"

namespace fs = std::filesystem;
using namespace mail;

namespace {

std::string message(const std::string &subject, const std::string &id,
                    const std::string &extra = "") {
    return "From: Ana Novak <ana@x.org>\r\nTo: me@x.org, Bob <bob@x.org>\r\n"
           "Cc: carol@x.org\r\nSubject: " +
           subject + "\r\nMessage-ID: <" + id +
           ">\r\nDate: Thu, 13 Feb 1975 23:31:30 +0000\r\n" + extra +
           "\r\nHello there\r\n-- \r\nAna\r\n";
}

bool matches(const char *text, const msg_info &info,
             const std::string &raw = "") {
    filter::tokenizer tokens(text);
    const auto list = filter::parse_condition_list(tokens);
    CHECK(list.has_value());
    return list && list->matches({info, [&]() -> std::optional<std::string> {
                                      return raw;
                                  },
                                  161566290 + 86400 * 3});
}

void test_conditions() {
    const std::string raw = message("Quarterly Report", "m@x",
                                    "X-Spam: yes\r\n");
    msg_info info = parse_msg_info(raw, 1);
    info.perm_flags = flag_unread;
    CHECK(matches("subject matchcase \"quarterly\"", info));
    CHECK(!matches("subject match \"quarterly\"", info));
    CHECK(matches("~subject match \"quarterly\"", info));
    CHECK(matches("subject regexp \"^Quarterly .*t$\"", info));
    CHECK(!matches("subject regexp \"[\"", info));
    CHECK(matches("from matchcase \"ana\" & unread", info));
    CHECK(!matches("from matchcase \"ana\" & marked", info));
    CHECK(matches("from matchcase \"nobody\" | unread", info));
    CHECK(matches("age_greater 2", info));
    CHECK(!matches("age_lower 2", info));
    CHECK(matches("header \"X-Spam\" matchcase \"yes\"", info, raw));
    CHECK(!matches("header \"X-Spam\" matchcase \"no\"", info, raw));
    CHECK(matches("body_part matchcase \"hello\"", info, raw));
    CHECK(matches("message matchcase \"x-spam\"", info, raw));
    CHECK(matches("to_or_cc matchcase \"carol\"", info));

    filter::tokenizer again("subject matchcase \"a\\\"b\" | ~colorlabel 3");
    const auto list = filter::parse_condition_list(again);
    CHECK(list.has_value() && !list->match_all);
    CHECK(list->to_string() ==
          "subject matchcase \"a\\\"b\" | ~colorlabel 3");
    filter::tokenizer wrong("nonsense");
    CHECK(!filter::parse_condition_list(wrong));
    filter::tokenizer open_quote("subject matchcase \"open");
    CHECK(!filter::parse_condition_list(open_quote));

    const auto quick = filter::parse_quick_search("report");
    CHECK(quick && quick->matches({info, {}, 0}));
    CHECK(!filter::parse_quick_search("zebra")->matches({info, {}, 0}));
    const auto extended = filter::parse_quick_search("f ana & U");
    CHECK(extended && extended->conditions.size() == 2);
    CHECK(extended->matches({info, {}, 0}));
    CHECK(!filter::parse_quick_search("f ana & T")->matches({info, {}, 0}));
    CHECK(filter::parse_quick_search("~U")->conditions[0].negated);
    CHECK(!filter::parse_quick_search("").has_value());
    // A bare word like "s" with nothing after it searches as plain text.
    CHECK(filter::parse_quick_search("s")->conditions.size() == 2);
}

void test_rules(const fs::path &base) {
    const auto parsed = filter::parse_rule(
        "enabled rulename \"lists\" account 2 subject matchcase \"[x]\" "
        "move \"#mh/Mailbox/work\" mark_as_read");
    CHECK(parsed.has_value());
    CHECK(parsed->name == "lists");
    CHECK(parsed->account_id == 2);
    CHECK(parsed->actions.size() == 2);
    CHECK(filter::parse_rule(parsed->to_string())->to_string() ==
          parsed->to_string());
    const auto unknown = filter::parse_rule(
        "all forward 0 \"a@x\" \"%q\" stop");
    CHECK(unknown.has_value());
    CHECK(unknown->actions.front().kind == filter::action_kind::unsupported);
    CHECK(unknown->to_string().find("forward") != std::string::npos);
    CHECK(!filter::parse_rule("subject matchcase \"x\"").has_value());

    const fs::path rc = base / "matcherrc";
    util::write_file(rc, "[preglobal]\nkept line\n[filtering]\n"
                         "disabled rulename \"a\" all mark\n"
                         "unreadable\n");
    filter::rule_set set;
    set.load(rc);
    CHECK(set.rules().size() == 1);
    CHECK(!set.rules()[0].enabled);
    CHECK(set.save(rc));
    CHECK(util::read_file(rc)->find("kept line") != std::string::npos);

    folder_list folders;
    auto &store = folders.add(
        std::make_unique<mh_folder>("Mailbox", base / "RuleMail"));
    folder_item *inbox = store.find_special(item_type::inbox);
    folder_item *work = store.create_folder(store.root(), "work");
    store.add(*inbox, message("[x] a", "1@x"), 0);
    store.add(*inbox, message("[x] b", "2@x"), 0);
    store.add(*inbox, message("plain", "3@x"), 0);
    inbox->scan();
    const std::vector<filter::rule> rules = {
        *filter::parse_rule("subject matchcase \"[x] a\" mark"),
        *filter::parse_rule("subject matchcase \"[x]\" mark_as_read "
                            "move \"#mh/Mailbox/work\""),
        *filter::parse_rule("subject matchcase \"[x]\" lock"),
        *filter::parse_rule("subject matchcase \"plain\" color 5 stop"),
        *filter::parse_rule("subject matchcase \"plain\" mark"),
    };
    CHECK(filter::apply_rules(rules, *inbox, {1, 2, 3}, folders) == 2);
    CHECK(inbox->total_count == 1);
    CHECK(work->messages().size() == 2);
    const msg_info &moved = work->messages().begin()->second;
    CHECK(moved.has(flag_marked));
    CHECK(!moved.has(flag_unread));
    CHECK(!moved.has(flag_locked)); // The rule list stopped at the move.
    CHECK(inbox->find(3)->color_label() == 5);
    CHECK(!inbox->find(3)->has(flag_marked));
}

void test_address_book(const fs::path &base) {
    const fs::path dir = base / "addrbook";
    {
        addr::book_list books;
        addr::book &book = books.primary();
        addr::person ana;
        ana.first_name = "Ana";
        ana.last_name = "Novak";
        ana.emails.push_back({"", "", "ana@x.org", ""});
        ana.emails.push_back({"", "work", "ana@work.org", ""});
        ana.attributes.push_back({"9", "phone", "1 & 2"});
        book.add(ana);
        addr::person bob;
        bob.common_name = "Bob <the> Builder";
        bob.emails.push_back({"", "", "bob@x.org", ""});
        const addr::person &stored = book.add(bob);
        addr::group team;
        team.uid = "50";
        team.name = "team";
        team.members.push_back({stored.uid, stored.emails[0].uid});
        book.groups().push_back(team);
        CHECK(books.save(dir));
    }
    addr::book_list books;
    books.load(dir);
    CHECK(books.books().size() == 1);
    const addr::book &book = books.books()[0];
    CHECK(book.people().size() == 2);
    CHECK(book.people()[0].attributes[0].value == "1 & 2");
    CHECK(book.people()[1].display_name() == "Bob <the> Builder");
    CHECK(books.contains("ANA@x.org"));
    CHECK(!books.contains("nobody@x.org"));
    auto found = books.complete("nov");
    CHECK(found.size() == 2);
    found = books.complete("ana@w");
    CHECK(found.size() == 1 && found[0].address == "ana@work.org");
    CHECK(found[0].formatted() == "Ana Novak <ana@work.org>");
    found = books.complete("tea");
    CHECK(found.size() == 1);
    CHECK(found[0].address.find("bob@x.org") != std::string::npos);
    CHECK(books.complete("").empty());
    CHECK(books.complete("zz").empty());
    addr::book_list changed;
    changed.load(dir);
    const std::string uid = changed.primary().people()[1].uid;
    changed.primary().remove_person(uid);
    CHECK(changed.primary().groups()[0].members.empty());

    const auto ldif = addr::import_ldif(
        "dn: cn=x\ncn: Ana N\ngivenName: Ana\nmail: a@x\n\n"
        "# skip\ndn: cn=y\ncn:: Q2FmZQ==\nmail: b@x\nmail: c@x\n\n"
        "dn: nomail\ncn: none\n");
    CHECK(ldif.size() == 2);
    CHECK(ldif[1].common_name == "Cafe");
    CHECK(ldif[1].emails.size() == 2);
    const auto cards = addr::import_vcard(
        "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Novak;Ana;;;\r\nFN:Ana Novak\r\n"
        "EMAIL;TYPE=work:ana@x.org\r\nEND:VCARD\r\n");
    CHECK(cards.size() == 1);
    CHECK(cards[0].last_name == "Novak");
    CHECK(cards[0].emails[0].address == "ana@x.org");
    CHECK(addr::import_ldif(addr::export_ldif(ldif)).size() == 3);
}

prefs::account make_account() {
    prefs::account account;
    account.id = 4;
    account.full_name = "Me Myself";
    account.address = "me@x.org";
    account.signature_text = "Regards";
    return account;
}

void test_compose() {
    using namespace compose;
    const prefs::account me = make_account();
    prefs::common settings;
    const std::string raw =
        message("Status", "orig@x", "References: <a@x> <b@x>\r\n");

    draft reply = reply_to(raw, me, settings, reply_mode::sender);
    CHECK(reply.to == "Ana Novak <ana@x.org>");
    CHECK(reply.cc.empty());
    CHECK(reply.subject == "Re: Status");
    CHECK(reply.in_reply_to == "<orig@x>");
    CHECK(reply.references == "<a@x> <b@x> <orig@x>");
    CHECK(reply.body.find("> Hello there\n") != std::string::npos);
    CHECK(reply.body.find("> Ana") == std::string::npos); // Signature dropped.
    CHECK(reply.body.find("wrote:") != std::string::npos);
    CHECK(reply.body.ends_with("-- \nRegards"));

    draft all = reply_to(raw, me, settings, reply_mode::all);
    CHECK(all.to == "Ana Novak <ana@x.org>");
    CHECK(all.cc == "Bob <bob@x.org>, carol@x.org");

    const std::string listed = message(
        "Re: Status", "l@x", "List-Post: <mailto:list@x.org?subject=a>\r\n"
                             "Reply-To: other@x.org\r\n");
    draft list = reply_to(listed, me, settings, reply_mode::list);
    CHECK(list.to == "list@x.org");
    CHECK(list.subject == "Re: Status");
    draft replied = reply_to(listed, me, settings, reply_mode::sender);
    CHECK(replied.to == "other@x.org");

    draft fwd = forward(raw, me, settings);
    CHECK(fwd.subject == "Fw: Status");
    CHECK(fwd.body.find("Hello there") != std::string::npos);
    CHECK(fwd.to.empty());
    CHECK(fwd.in_reply_to.empty());

    prefs::account_list accounts;
    accounts.add(make_account());
    prefs::account second = make_account();
    second.address = "bob@x.org";
    const int id = accounts.add(second);
    CHECK(account_for_reply("To: Bob <bob@x.org>\n\n", accounts)->id == id);
    CHECK(account_for_reply("To: nobody@x.org\n\n", accounts) ==
          accounts.default_account());

    mail::msg_info info = mail::parse_msg_info(raw, 1);
    CHECK(expand_format("%F %L %I %s %%\\tx\\n%af %am %Z", info, "b", "> ",
                        me) ==
          "Ana Novak AN Status %\tx\nMe Myself me@x.org %Z");
    std::size_t cursor = 0;
    CHECK(expand_format("ab%Xc", info, "", "> ", me, &cursor) == "abc");
    CHECK(cursor == 2);
    CHECK(quote_text("a\n> b\n", "> ") == "> a\n>> b\n");
    CHECK(strip_signature("a\n-- \nsig\n") == "a\n");

    CHECK(wrap_text("short\n", 72) == "short\n");
    CHECK(wrap_text("aaa bbb ccc ddd eee fff ggg hhh iii jjj", 20) ==
          "aaa bbb ccc ddd eee\nfff ggg hhh iii jjj");
    CHECK(wrap_text("> aaa bbb ccc ddd eee fff ggg hhh", 20) ==
          "> aaa bbb ccc ddd\n> eee fff ggg hhh");
    const std::string url(50, 'u');
    CHECK(wrap_text("see " + url + " ok", 20) ==
          "see\n" + url + "\nok");
    CHECK(wrap_text("body\n-- \n" + std::string(40, 's') + " x\n", 20) ==
          "body\n-- \n" + std::string(40, 's') + " x\n");

    draft text = new_draft(me);
    CHECK(text.body == "-- \nRegards");
    text.to = "a@x, b@x";
    text.cc = "c@x";
    text.bcc = "d@x";
    text.subject = "Test";
    text.body = "hello";
    text.attachments.push_back({"", "note.txt", "", "data"});
    CHECK(text.recipients().size() == 4);
    const auto outgoing = to_outgoing(text, me, settings, 161566290);
    CHECK(outgoing.has_value());
    CHECK(outgoing->message_id.ends_with("@x.org"));
    CHECK(outgoing->attachments[0].mime_type == "text/plain");
    draft bad = text;
    bad.attachments.push_back({"/no/such/file", "", "", ""});
    CHECK(!to_outgoing(bad, me, settings, 1).has_value());

    const std::string rendered = mime::render_message(*outgoing);
    const draft reopened = reopen(rendered);
    CHECK(reopened.to == "a@x, b@x");
    CHECK(reopened.body == "hello");
    CHECK(reopened.attachments.size() == 1);
    CHECK(reopened.attachments[0].data == "data");

    queued_message entry;
    entry.sender = "me@x.org";
    entry.smtp_server = "s";
    entry.recipients = {"a@x", "b@x"};
    entry.account_id = 4;
    entry.sent_folder = "#mh/Mailbox/sent";
    entry.reply_source = "#mh/Mailbox/inbox\t7";
    entry.message = rendered;
    const queued_message back = read_queue_entry(write_queue_entry(entry));
    CHECK(back.sender == "me@x.org");
    CHECK(back.recipients == entry.recipients);
    CHECK(back.account_id == 4);
    CHECK(back.sent_folder == entry.sent_folder);
    CHECK(back.reply_source == entry.reply_source);
    CHECK(back.message == rendered);
    CHECK(read_queue_entry("Subject: x\n\ny\n").recipients.empty());
}

msg_info summary(std::uint32_t number, const std::string &subject,
                 const std::string &id, const std::string &parent,
                 std::time_t when) {
    msg_info info;
    info.number = number;
    info.subject = subject;
    info.message_id = id;
    info.in_reply_to = parent;
    info.date_time = when;
    info.from_name = "z" + std::to_string(10 - number);
    return info;
}

void test_summary() {
    view::summary_model model;
    model.set_messages({summary(1, "root", "a", "", 100),
                        summary(2, "Re: root", "b", "a", 300),
                        summary(3, "Re: root", "c", "a", 200),
                        summary(4, "deep", "d", "c", 400),
                        summary(5, "other", "e", "", 50),
                        summary(6, "orphan", "f", "gone", 60),
                        summary(7, "loop1", "g", "h", 70),
                        summary(8, "loop2", "h", "g", 80)});
    CHECK(model.rows().size() == 8);
    CHECK(model.at(0).number == 5); // Date order, oldest first.
    model.set_threaded(true);
    std::vector<std::uint32_t> order;
    std::vector<int> depths;
    for (std::size_t i = 0; i < model.rows().size(); ++i) {
        order.push_back(model.at(i).number);
        depths.push_back(model.rows()[i].depth);
    }
    CHECK(order == (std::vector<std::uint32_t>{5, 6, 7, 8, 1, 3, 4, 2}) ||
          order == (std::vector<std::uint32_t>{5, 6, 8, 7, 1, 3, 4, 2}));
    CHECK(model.rows().size() == 8); // Cycles lose nothing.
    CHECK(depths[5] == 1);
    CHECK(depths[6] == 2);
    CHECK(model.rows()[4].has_children);
    model.set_sort(sort_key::date, false);
    CHECK(model.at(0).number == 1);
    model.set_threaded(false);
    model.set_sort(sort_key::subject, true);
    CHECK(model.at(0).subject == "deep");
    model.set_sort(sort_key::from, true);
    CHECK(model.at(0).number == 8);
    model.set_sort(sort_key::number, false);
    CHECK(model.at(0).number == 8);
    CHECK(model.row_of(1) == 7);
    CHECK(!model.row_of(99));

    model.set_sort(sort_key::number, true);
    CHECK(!model.first_unread());
    model.set_flags(3, flag_unread);
    model.set_flags(6, flag_unread);
    CHECK(model.first_unread() == 2);
    CHECK(model.next_unread(2) == 5);
    CHECK(!model.next_unread(5));
    CHECK(model.previous_unread(5) == 2);
    CHECK(!model.previous_unread(2));
    model.set_hide_read(true);
    CHECK(model.rows().size() == 2);
    model.set_hide_read(false);
    model.set_filter(filter::parse_quick_search("orphan"));
    CHECK(model.rows().size() == 1);
    model.set_filter(std::nullopt);
    CHECK(model.rows().size() == 8);
    model.set_matches(std::set<std::uint32_t>{2, 4});
    CHECK(model.rows().size() == 2);
    model.set_matches(std::nullopt);
    CHECK(model.rows().size() == 8);
    CHECK(filter::parse_quick_search("b x")->needs_content());
    CHECK(!filter::parse_quick_search("s x")->needs_content());

    // A chain longer than any recursion would survive.
    std::vector<msg_info> chain;
    for (std::uint32_t i = 1; i <= 50000; ++i)
        chain.push_back(summary(i, "s", std::to_string(i),
                                i > 1 ? std::to_string(i - 1) : "",
                                static_cast<std::time_t>(i)));
    view::summary_model deep;
    deep.set_threaded(true);
    deep.set_messages(std::move(chain));
    CHECK(deep.rows().size() == 50000);
    CHECK(deep.rows().back().depth == 49999);

    msg_info flags;
    flags.perm_flags = flag_new | flag_unread | flag_marked | flag_replied;
    flags.temp_flags = temp_has_attachment;
    CHECK(view::status_text(flags) == "NR!@");
    CHECK(view::size_text(512) == "512B");
    CHECK(view::size_text(2048) == "2.0KB");
    CHECK(view::size_text(3 * 1024 * 1024) == "3.0MB");
    msg_info empty;
    CHECK(view::subject_text(empty, 1) == "  (No Subject)");
}

void test_document() {
    CHECK(view::quote_depth("plain") == 0);
    CHECK(view::quote_depth("> a") == 1);
    CHECK(view::quote_depth(">> a") == 2);
    CHECK(view::quote_depth("> > | a") == 3);
    const std::string raw =
        "Subject: =?UTF-8?Q?caf=C3=A9?=\r\nFrom: a@x\r\nX-Hidden: 1\r\n"
        "Content-Type: multipart/mixed; boundary=B\r\n\r\n"
        "--B\r\nContent-Type: text/plain\r\n\r\n"
        "see http://x.org/a. and (www.y.org)\r\n> quoted\r\n>> deeper\r\n"
        "-- \r\nsig\r\n"
        "--B\r\nContent-Type: image/png\r\n"
        "Content-Disposition: attachment; filename=a.png\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\nTWFu\r\n--B--\r\n";
    const view::document doc = view::render_document(raw);
    CHECK(doc.text.find("Subject: caf\xC3\xA9") != std::string::npos);
    CHECK(doc.text.find("X-Hidden") == std::string::npos);
    CHECK(view::render_document(raw, true).text.find("X-Hidden") !=
          std::string::npos);
    CHECK(doc.links.size() == 2);
    CHECK(doc.links[0].target == "http://x.org/a");
    CHECK(doc.links[1].target == "http://www.y.org");
    CHECK(doc.link_at(doc.links[0].start) == &doc.links[0]);
    CHECK(doc.link_at(0) == nullptr);
    CHECK(doc.attachments.size() == 1);
    CHECK(doc.attachments[0].name == "a.png");
    CHECK(doc.attachment_at(doc.attachments[0].line_start) == 0);
    CHECK(!doc.attachment_at(0));
    CHECK(view::attachment_data(raw, 0) == "Man");
    CHECK(view::attachment_data(raw, 5).empty());
    const auto has = [&](view::text_style style) {
        for (const view::styled_span &span : doc.spans)
            if (span.style == style)
                return true;
        return false;
    };
    CHECK(has(view::text_style::quote_1));
    CHECK(has(view::text_style::quote_2));
    CHECK(has(view::text_style::signature));
    CHECK(has(view::text_style::link));
    CHECK(has(view::text_style::subject));
    for (std::size_t i = 1; i < doc.spans.size(); ++i)
        CHECK(doc.spans[i].start >= doc.spans[i - 1].end);
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() /
                          ("blaise-logic-" + std::to_string(getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    test_conditions();
    test_rules(base);
    test_address_book(base);
    test_compose();
    test_summary();
    test_document();
    fs::remove_all(base);
    return testing::summary("logic");
}
