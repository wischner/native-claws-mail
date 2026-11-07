//
// Mail accounts.
//
// Passwords live apart from accountrc, in a file only the user can read.
// Claws encrypts its store under a master passphrase; Blaise does not
// yet, which docs/notes/PORT-STATUS.md records.
//
// Derived from Claws Mail src/prefs_account.c and src/account.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "prefs/account.h"

#include <algorithm>
#include <cstdlib>

#include "mime/address.h"
#include "util/codec.h"
#include "util/text.h"

namespace prefs {
namespace {

namespace fs = std::filesystem;

constexpr const char *account_file = "accountrc";
constexpr const char *password_file = "passwordrc";

// Return the section label of an account.
std::string label_for(int id) { return "Account: " + std::to_string(id); }

// Return the password map key.
std::string secret_key(int id, bool smtp) {
    return std::to_string(id) + (smtp ? ":smtp" : ":recv");
}

// The port pair Claws stores: a set flag and a value.
int read_port(const rc_section &from, const char *flag, const char *key) {
    return from.get_bool(flag) ? from.get_int(key) : 0;
}

// Write a port pair.
void write_port(rc_section &to, const char *flag, const char *key,
                int port) {
    to.set_bool(flag, port != 0);
    if (port != 0)
        to.set_int(key, port);
}

// Read a folder pair: a set flag and an identifier.
std::string read_folder(const rc_section &from, const char *flag,
                        const char *key) {
    return from.get_bool(flag) ? from.get(key) : std::string();
}

// Write a folder pair.
void write_folder(rc_section &to, const char *flag, const char *key,
                  const std::string &value) {
    to.set_bool(flag, !value.empty());
    to.set(key, value);
}

// Read an optional string guarded by a set flag.
std::string read_auto(const rc_section &from, const char *flag,
                      const char *key) {
    return from.get_bool(flag) ? from.get(key) : std::string();
}

// Build an account from its section.
account read_account(const rc_section &from, int id) {
    account value;
    value.id = id;
    value.account_name = from.get("account_name");
    value.is_default = from.get_bool("is_default");
    value.full_name = from.get("name");
    value.address = from.get("address");
    value.organization = from.get("organization");
    value.kind = static_cast<protocol>(from.get_int("protocol", 1));
    value.receive_server = from.get("receive_server");
    value.smtp_server = from.get("smtp_server");
    value.user_id = from.get("user_id");
    value.smtp_user_id = from.get("smtp_user_id");
    value.use_smtp_auth = from.get_bool("use_smtp_auth", true);
    value.use_apop = from.get_bool("use_apop_auth");
    value.remove_mail = from.get_bool("remove_mail", true);
    value.filter_on_receive = from.get_bool("filter_on_receive", true);
    value.receive_at_get_all = from.get_bool("receive_at_get_all", true);
    value.inbox = from.get("inbox", "#mh/Mailbox/inbox");
    value.imap_directory = from.get("imap_directory");
    value.imap_subscribed_only = from.get_bool("imap_subsonly", true);
    value.local_mbox = from.get("local_mbox", "/var/mail");
    value.signature_text = from.get("signature_text");
    value.auto_signature = from.get_bool("auto_signature", true);
    value.signature_separator = from.get("signature_separator", "-- ");
    value.auto_cc = read_auto(from, "set_autocc", "auto_cc");
    value.auto_bcc = read_auto(from, "set_autobcc", "auto_bcc");
    value.auto_reply_to = read_auto(from, "set_autoreplyto", "auto_replyto");
    value.smtp_port = read_port(from, "set_smtpport", "smtp_port");
    switch (value.kind) {
    case protocol::pop3:
        value.receive_port = read_port(from, "set_popport", "pop_port");
        value.receive_security =
            static_cast<security>(from.get_int("ssl_pop", 0));
        break;
    case protocol::imap4:
        value.receive_port = read_port(from, "set_imapport", "imap_port");
        value.receive_security =
            static_cast<security>(from.get_int("ssl_imap", 0));
        break;
    default:
        break;
    }
    value.smtp_security = static_cast<security>(from.get_int("ssl_smtp", 0));
    value.sent_folder = read_folder(from, "set_sent_folder", "sent_folder");
    value.queue_folder =
        read_folder(from, "set_queue_folder", "queue_folder");
    value.draft_folder =
        read_folder(from, "set_draft_folder", "draft_folder");
    value.trash_folder =
        read_folder(from, "set_trash_folder", "trash_folder");
    value.accept_invalid_certificate =
        from.get_bool("blaise_accept_invalid_certificate");
    return value;
}

// Copy an account into its section.
void write_account(rc_section &to, const account &value) {
    to.set("account_name", value.account_name);
    to.set_bool("is_default", value.is_default);
    to.set("name", value.full_name);
    to.set("address", value.address);
    to.set("organization", value.organization);
    to.set_int("protocol", static_cast<int>(value.kind));
    to.set("receive_server", value.receive_server);
    to.set("smtp_server", value.smtp_server);
    to.set("user_id", value.user_id);
    to.set("smtp_user_id", value.smtp_user_id);
    to.set_bool("use_smtp_auth", value.use_smtp_auth);
    to.set_bool("use_apop_auth", value.use_apop);
    to.set_bool("remove_mail", value.remove_mail);
    to.set_bool("filter_on_receive", value.filter_on_receive);
    to.set_bool("receive_at_get_all", value.receive_at_get_all);
    to.set("inbox", value.inbox);
    to.set("imap_directory", value.imap_directory);
    to.set_bool("imap_subsonly", value.imap_subscribed_only);
    to.set("local_mbox", value.local_mbox);
    to.set("signature_text", value.signature_text);
    to.set_bool("auto_signature", value.auto_signature);
    to.set("signature_separator", value.signature_separator);
    to.set_bool("set_autocc", !value.auto_cc.empty());
    to.set("auto_cc", value.auto_cc);
    to.set_bool("set_autobcc", !value.auto_bcc.empty());
    to.set("auto_bcc", value.auto_bcc);
    to.set_bool("set_autoreplyto", !value.auto_reply_to.empty());
    to.set("auto_replyto", value.auto_reply_to);
    write_port(to, "set_smtpport", "smtp_port", value.smtp_port);
    if (value.kind == protocol::pop3) {
        write_port(to, "set_popport", "pop_port", value.receive_port);
        to.set_int("ssl_pop", static_cast<int>(value.receive_security));
    } else if (value.kind == protocol::imap4) {
        write_port(to, "set_imapport", "imap_port", value.receive_port);
        to.set_int("ssl_imap", static_cast<int>(value.receive_security));
    }
    to.set_int("ssl_smtp", static_cast<int>(value.smtp_security));
    write_folder(to, "set_sent_folder", "sent_folder", value.sent_folder);
    write_folder(to, "set_queue_folder", "queue_folder",
                 value.queue_folder);
    write_folder(to, "set_draft_folder", "draft_folder",
                 value.draft_folder);
    write_folder(to, "set_trash_folder", "trash_folder",
                 value.trash_folder);
    to.set_bool("blaise_accept_invalid_certificate",
                value.accept_invalid_certificate);
}

} // namespace

int account::effective_receive_port() const {
    if (receive_port != 0)
        return receive_port;
    const bool wrapped = receive_security == security::tls;
    switch (kind) {
    case protocol::pop3:
        return wrapped ? 995 : 110;
    case protocol::imap4:
        return wrapped ? 993 : 143;
    case protocol::nntp:
        return wrapped ? 563 : 119;
    default:
        return 0;
    }
}

int account::effective_smtp_port() const {
    if (smtp_port != 0)
        return smtp_port;
    switch (smtp_security) {
    case security::tls:
        return 465;
    case security::starttls:
        return 587;
    case security::none:
        break;
    }
    return 25;
}

const std::string &account::effective_smtp_user() const {
    return smtp_user_id.empty() ? user_id : smtp_user_id;
}

std::string account::from_header() const {
    return mime::format_mailbox({full_name, address});
}

void account_list::load(const fs::path &directory) {
    accounts_.clear();
    file_ = rc_file::load(directory / account_file);
    for (const rc_section &block : file_.sections()) {
        const std::string &label = block.label();
        if (!label.starts_with("Account: "))
            continue;
        const auto id = util::parse_int(std::string_view(label).substr(9));
        if (id)
            accounts_.push_back(
                read_account(block, static_cast<int>(*id)));
    }
    passwords_.clear();
    const rc_file secrets = rc_file::load(directory / password_file);
    if (const rc_section *block = secrets.find("Passwords"))
        for (const auto &[key, value] : block->entries())
            passwords_[key] = util::base64_decode(value);
}

bool account_list::save(const fs::path &directory) {
    for (const account &value : accounts_)
        write_account(file_.section(label_for(value.id)), value);
    std::vector<std::string> gone;
    for (const rc_section &block : file_.sections()) {
        if (!block.label().starts_with("Account: "))
            continue;
        const auto id =
            util::parse_int(std::string_view(block.label()).substr(9));
        if (!id || find(static_cast<int>(*id)) == nullptr)
            gone.push_back(block.label());
    }
    for (const std::string &label : gone)
        file_.remove(label);
    rc_file secrets;
    rc_section &block = secrets.section("Passwords");
    for (const auto &[key, value] : passwords_)
        block.set(key, util::base64_encode(value));
    const bool wrote_accounts = file_.save(directory / account_file);
    return secrets.save(directory / password_file, true) && wrote_accounts;
}

const account *account_list::find(int id) const {
    const auto it = std::ranges::find(accounts_, id, &account::id);
    return it == accounts_.end() ? nullptr : &*it;
}

const account *account_list::default_account() const {
    const auto it =
        std::ranges::find_if(accounts_, &account::is_default);
    if (it != accounts_.end())
        return &*it;
    return accounts_.empty() ? nullptr : &accounts_.front();
}

int account_list::add(account value) {
    int id = 1;
    for (const account &existing : accounts_)
        id = std::max(id, existing.id + 1);
    value.id = id;
    accounts_.push_back(std::move(value));
    return id;
}

void account_list::remove(int id) {
    std::erase_if(accounts_,
                  [&](const account &item) { return item.id == id; });
    passwords_.erase(secret_key(id, false));
    passwords_.erase(secret_key(id, true));
}

void account_list::set_default(int id) {
    for (account &item : accounts_)
        item.is_default = item.id == id;
}

std::string account_list::password(int id, bool smtp) const {
    const auto it = passwords_.find(secret_key(id, smtp));
    return it == passwords_.end() ? std::string() : it->second;
}

void account_list::set_password(int id, bool smtp,
                                const std::string &secret) {
    if (secret.empty())
        passwords_.erase(secret_key(id, smtp));
    else
        passwords_[secret_key(id, smtp)] = secret;
}

} // namespace prefs
