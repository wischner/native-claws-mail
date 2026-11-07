//
// Mail accounts. PrefsAccount's two hundred fields become the ones that
// are ported; each account also keeps the section it was read from, so
// an accountrc written by Claws keeps every other setting when Blaise
// saves it.
//
// Derived from Claws Mail src/prefs_account.c and src/account.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef PREFS_ACCOUNT_H
#define PREFS_ACCOUNT_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "prefs/rc_file.h"

namespace prefs {

// How an account receives mail; values match Claws' accountrc.
enum class protocol { pop3 = 0, imap4 = 1, nntp = 2, local = 3, none = 4 };

// How a connection is secured; values match Claws' accountrc.
enum class security { none = 0, tls = 1, starttls = 2 };

// One account.
struct account {
    int id = 0;
    std::string account_name;
    bool is_default = false;
    std::string full_name;
    std::string address;
    std::string organization;
    protocol kind = protocol::imap4;
    std::string receive_server;
    std::string smtp_server;
    std::string user_id;
    std::string smtp_user_id;  // Empty means user_id.
    int receive_port = 0;      // Zero means the protocol default.
    int smtp_port = 0;
    security receive_security = security::tls;
    security smtp_security = security::starttls;
    bool use_smtp_auth = true;
    bool use_apop = false;
    bool remove_mail = true;   // POP3: delete from the server.
    bool filter_on_receive = true;
    bool receive_at_get_all = true;
    std::string inbox = "#mh/Mailbox/inbox";
    std::string imap_directory;
    bool imap_subscribed_only = true;
    std::string local_mbox = "/var/mail";
    std::string signature_text;
    bool auto_signature = true;
    std::string signature_separator = "-- ";
    std::string auto_cc;
    std::string auto_bcc;
    std::string auto_reply_to;
    std::string sent_folder;
    std::string queue_folder;
    std::string draft_folder;
    std::string trash_folder;
    bool accept_invalid_certificate = false;

    // Return the port to receive on, applying protocol defaults.
    int effective_receive_port() const;

    // Return the port to send on, applying defaults.
    int effective_smtp_port() const;

    // Return the login for SMTP.
    const std::string &effective_smtp_user() const;

    // Return "Name <address>", as the From header wants it.
    std::string from_header() const;
};

// Every account, with their on-disk file and stored secrets.
class account_list {
public:
    // Read accounts and passwords from a configuration directory.
    void load(const std::filesystem::path &directory);

    // Write accounts and passwords; returns whether both succeeded.
    bool save(const std::filesystem::path &directory);

    // Return all accounts in file order.
    std::vector<account> &all() { return accounts_; }
    const std::vector<account> &all() const { return accounts_; }

    // Return an account by id, or null.
    const account *find(int id) const;

    // Return the default account, or the first, or null.
    const account *default_account() const;

    // Add an account, giving it a fresh id; returns the id.
    int add(account value);

    // Remove an account.
    void remove(int id);

    // Mark one account as the default.
    void set_default(int id);

    // Return a stored password, or an empty string.
    std::string password(int id, bool smtp) const;

    // Store a password; empty forgets it.
    void set_password(int id, bool smtp, const std::string &secret);

private:
    std::vector<account> accounts_;
    rc_file file_;
    std::map<std::string, std::string> passwords_;
};

} // namespace prefs

#endif
