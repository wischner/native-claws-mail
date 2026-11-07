//
// Account settings.
//
// Derived from Claws Mail src/prefs_account.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "account_window.h"

#include <algorithm>

#include "util/text.h"

namespace ui {
namespace {

constexpr int list_width = 170;
constexpr int label_x = list_width + 24;
constexpr int field_x = label_x + 118;
constexpr int field_width = 290;
constexpr int row = 31;

// Return the y of a form row.
constexpr int row_y(int index) { return 12 + index * row; }

// Return the index of a protocol in the combo box. News and local
// accounts come from Claws and have no row: they show as sending only.
int protocol_index(prefs::protocol kind) {
    switch (kind) {
    case prefs::protocol::imap4:
        return 0;
    case prefs::protocol::pop3:
        return 1;
    default:
        return 2;
    }
}

// Return a port from its field, or 0 for the default.
int read_port(const std::string &text) {
    const auto value = util::parse_int(util::trim(text));
    return value && *value > 0 && *value < 65536 ? static_cast<int>(*value)
                                                 : 0;
}

// Return the text of a port.
std::string port_text(int port) {
    return port == 0 ? std::string() : std::to_string(port);
}

} // namespace

account_window::account_window(native::app_wnd &owner,
                               std::function<void(prefs::account_list)> done)
    : form_window(owner, "Accounts", 640, 520), done_(std::move(done)),
      rows_({}, 12, 12, list_width, 380), add_("New", 12, 400, 54, 28),
      remove_("Delete", 70, 400, 54, 28), default_("Default", 128, 400, 54, 28),
      name_("", native::text_edit_mode::single_line, field_x, row_y(0),
            field_width, 26),
      full_name_("", native::text_edit_mode::single_line, field_x, row_y(1),
                 field_width, 26),
      address_("", native::text_edit_mode::single_line, field_x, row_y(2),
               field_width, 26),
      protocol_({"IMAP", "POP3", "Send only"},
                native::combo_box_style::drop_down_list,
                field_x, row_y(3), 110, 26),
      server_("", native::text_edit_mode::single_line, field_x, row_y(4),
              field_width, 26),
      port_("", native::text_edit_mode::single_line, field_x, row_y(5), 70,
            26),
      security_({"None", "TLS", "STARTTLS"},
                native::combo_box_style::drop_down_list, field_x + 80,
                row_y(5), 120, 26),
      user_("", native::text_edit_mode::single_line, field_x, row_y(6),
            field_width, 26),
      password_(field_x, row_y(7), field_width, 26),
      smtp_server_("", native::text_edit_mode::single_line, field_x, row_y(8),
                   field_width, 26),
      smtp_port_("", native::text_edit_mode::single_line, field_x, row_y(9),
                 70, 26),
      smtp_security_({"None", "TLS", "STARTTLS"},
                     native::combo_box_style::drop_down_list, field_x + 80,
                     row_y(9), 120, 26),
      smtp_auth_("Log in to send", label_x, row_y(10), 180, 24),
      remove_mail_("Delete from POP3 server", label_x + 190, row_y(10), 230,
                   24),
      signature_("", native::text_edit_mode::multi_line, field_x, row_y(11),
                 field_width, 60) {
    add_caption("Account name", label_x, row_y(0));
    add_caption("Full name", label_x, row_y(1));
    add_caption("Mail address", label_x, row_y(2));
    add_caption("Protocol", label_x, row_y(3));
    add_caption("Server", label_x, row_y(4));
    add_caption("Port, security", label_x, row_y(5));
    add_caption("User", label_x, row_y(6));
    add_caption("Password", label_x, row_y(7));
    add_caption("SMTP server", label_x, row_y(8));
    add_caption("SMTP port", label_x, row_y(9));
    add_caption("Signature", label_x, row_y(11));
    rows_.on_selection_change.connect([this](int index) {
        select(index);
        return true;
    });
    add_.on_click.connect([this] {
        add_account();
        return true;
    });
    remove_.on_click.connect([this] {
        remove_account();
        return true;
    });
    default_.on_click.connect([this] {
        make_default();
        return true;
    });
}

void account_window::populate() {
    for (native::wnd *child : std::initializer_list<native::wnd *>{
             &rows_, &add_, &remove_, &default_, &name_, &full_name_,
             &address_, &protocol_, &server_, &port_, &security_, &user_,
             &password_, &smtp_server_, &smtp_port_, &smtp_security_,
             &smtp_auth_, &remove_mail_, &signature_})
        attach(*child);
}

void account_window::edit(prefs::account_list accounts) {
    accounts_ = std::move(accounts);
    current_ = -1;
    open();
}

void account_window::prepare() {
    show_rows();
    select(accounts_.all().empty() ? -1 : 0);
}

void account_window::show_rows() {
    std::vector<std::string> labels;
    for (const prefs::account &item : accounts_.all()) {
        std::string label = item.account_name.empty() ? item.address
                                                      : item.account_name;
        labels.push_back(label + (item.is_default ? " *" : ""));
    }
    rows_.set_items(std::move(labels));
}

void account_window::select(int index) {
    store_fields();
    current_ = index;
    updating_ = true;
    rows_.set_selected_index(index);
    load_fields();
    updating_ = false;
}

void account_window::load_fields() {
    prefs::account blank;
    const bool any = current_ >= 0;
    const prefs::account &item =
        any ? accounts_.all()[static_cast<std::size_t>(current_)] : blank;
    name_.set_text(item.account_name);
    full_name_.set_text(item.full_name);
    address_.set_text(item.address);
    protocol_.set_selected_index(protocol_index(item.kind));
    server_.set_text(item.receive_server);
    port_.set_text(port_text(item.receive_port));
    security_.set_selected_index(static_cast<int>(item.receive_security));
    user_.set_text(item.user_id);
    password_.set_text(any ? accounts_.password(item.id, false)
                           : std::string());
    smtp_server_.set_text(item.smtp_server);
    smtp_port_.set_text(port_text(item.smtp_port));
    smtp_security_.set_selected_index(static_cast<int>(item.smtp_security));
    smtp_auth_.set_checked(item.use_smtp_auth);
    remove_mail_.set_checked(item.remove_mail);
    signature_.set_text(item.signature_text);
}

void account_window::store_fields() {
    if (current_ < 0)
        return;
    prefs::account &item =
        accounts_.all()[static_cast<std::size_t>(current_)];
    item.account_name = name_.get_text();
    item.full_name = full_name_.get_text();
    item.address = address_.get_text();
    // Only an actual change touches the protocol, or a Claws news account
    // would turn into "send only" by being looked at.
    const int chosen = protocol_.get_selected_index();
    if (chosen != protocol_index(item.kind))
        item.kind = chosen == 0   ? prefs::protocol::imap4
                    : chosen == 1 ? prefs::protocol::pop3
                                  : prefs::protocol::none;
    item.receive_server = server_.get_text();
    item.receive_port = read_port(port_.get_text());
    item.receive_security =
        static_cast<prefs::security>(security_.get_selected_index());
    item.user_id = user_.get_text();
    accounts_.set_password(item.id, false, password_.get_text());
    item.smtp_server = smtp_server_.get_text();
    item.smtp_port = read_port(smtp_port_.get_text());
    item.smtp_security =
        static_cast<prefs::security>(smtp_security_.get_selected_index());
    item.use_smtp_auth = smtp_auth_.get_checked();
    item.remove_mail = remove_mail_.get_checked();
    item.signature_text = signature_.get_text();
}

void account_window::add_account() {
    store_fields();
    prefs::account item;
    item.account_name = "New account";
    item.is_default = accounts_.all().empty();
    accounts_.add(std::move(item));
    current_ = -1;
    show_rows();
    select(static_cast<int>(accounts_.all().size()) - 1);
}

void account_window::remove_account() {
    if (current_ < 0)
        return;
    accounts_.remove(accounts_.all()[static_cast<std::size_t>(current_)].id);
    current_ = -1;
    show_rows();
    select(accounts_.all().empty() ? -1 : 0);
}

void account_window::make_default() {
    if (current_ < 0)
        return;
    store_fields();
    accounts_.set_default(
        accounts_.all()[static_cast<std::size_t>(current_)].id);
    const int keep = current_;
    show_rows();
    current_ = -1;
    select(keep);
}

bool account_window::accept() {
    store_fields();
    for (const prefs::account &item : accounts_.all())
        if (item.address.find('@') == std::string::npos) {
            native::message_box::show(
                *this, "Each account needs a mail address.", "Accounts",
                native::message_box_buttons::ok,
                native::message_box_icon::warning);
            return false;
        }
    if (done_) {
        const auto done = done_;
        prefs::account_list list = accounts_;
        native::app::post([done, list] { done(list); });
    }
    return true;
}

} // namespace ui
