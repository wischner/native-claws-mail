//
// Building drafts.
//
// Derived from Claws Mail src/compose.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "compose/draft.h"

#include <algorithm>

#include "compose/queue.h"

#include "mime/address.h"
#include "mime/encoded_word.h"
#include "mime/header.h"
#include "mime/part.h"
#include "util/files.h"
#include "util/text.h"

namespace compose {
namespace {

constexpr const char *user_agent_name = "Blaise " BLAISE_VERSION;

// Add "Re: " unless it is already there.
std::string reply_subject(std::string_view subject) {
    return util::istarts_with(subject, "re:")
               ? std::string(subject)
               : "Re: " + std::string(subject);
}

// Add "Fw: " unless it is already there.
std::string forward_subject(std::string_view subject) {
    return util::istarts_with(subject, "fw:") ||
                   util::istarts_with(subject, "fwd:")
               ? std::string(subject)
               : "Fw: " + std::string(subject);
}

// Return the address in a List-Post value, or empty.
std::string list_post_address(std::string_view value) {
    const std::size_t open = util::ifind(value, "<mailto:");
    if (open == std::string_view::npos)
        return {};
    const std::size_t close = value.find('>', open);
    if (close == std::string_view::npos)
        return {};
    std::string address(value.substr(open + 8, close - open - 8));
    address.resize(std::min(address.size(), address.find('?')));
    return address;
}

// Return mailboxes without an address and without duplicates.
std::vector<mime::mailbox> remove_address(std::vector<mime::mailbox> boxes,
                                          std::string_view address) {
    std::vector<mime::mailbox> kept;
    for (mime::mailbox &box : boxes) {
        if (util::iequals(box.address, address))
            continue;
        if (std::ranges::any_of(kept, [&](const mime::mailbox &other) {
                return util::iequals(other.address, box.address);
            }))
            continue;
        kept.push_back(std::move(box));
    }
    return kept;
}

// Return the References value for a reply.
std::string references_for(const mail::msg_info &info) {
    std::vector<std::string> ids;
    for (const std::string &id : info.references)
        ids.push_back("<" + id + ">");
    if (!info.message_id.empty())
        ids.push_back("<" + info.message_id + ">");
    // Headers must stay sendable: keep the first and the most recent.
    constexpr std::size_t limit = 20;
    if (ids.size() > limit)
        ids.erase(ids.begin() + 1, ids.end() - (limit - 1));
    return util::join(ids, " ");
}

// Fill recipients for a reply mode.
void fill_recipients(draft &out, const mime::header_block &headers,
                     const prefs::account &account, reply_mode mode) {
    const std::string sender = mime::decode_words(
        headers.find("Reply-To").value_or(headers.get("From")));
    if (mode == reply_mode::list) {
        const std::string list = list_post_address(headers.get("List-Post"));
        if (!list.empty()) {
            out.to = list;
            return;
        }
    }
    const auto follow_up = headers.find("Mail-Followup-To");
    if (mode == reply_mode::all && follow_up) {
        out.to = mime::format_addresses(remove_address(
            mime::parse_addresses(mime::decode_words(*follow_up)),
            account.address));
        return;
    }
    out.to = sender;
    if (mode != reply_mode::all)
        return;
    auto others = mime::parse_addresses(mime::decode_words(headers.get("To")));
    const auto copied =
        mime::parse_addresses(mime::decode_words(headers.get("Cc")));
    others.insert(others.end(), copied.begin(), copied.end());
    const mime::mailbox replied = mime::parse_address(sender);
    out.cc = mime::format_addresses(remove_address(
        remove_address(std::move(others), account.address), replied.address));
    // Replying to our own message: the original recipients are the target.
    if (util::iequals(replied.address, account.address) && !out.cc.empty()) {
        out.to = out.cc;
        out.cc.clear();
    }
}

} // namespace

std::vector<std::string> draft::recipients() const {
    std::vector<std::string> addresses;
    for (const std::string *field : {&to, &cc, &bcc})
        for (const mime::mailbox &box : mime::parse_addresses(*field))
            addresses.push_back(box.address);
    return addresses;
}

std::string with_signature(std::string_view body,
                           const prefs::account &account) {
    std::string out(body);
    if (account.signature_text.empty() || !account.auto_signature)
        return out;
    if (!out.empty() && !out.ends_with('\n'))
        out += '\n';
    out += account.signature_separator + "\n" + account.signature_text;
    return out;
}

draft new_draft(const prefs::account &account) {
    draft out;
    out.account_id = account.id;
    out.from = account.from_header();
    out.cc = account.auto_cc;
    out.bcc = account.auto_bcc;
    out.reply_to = account.auto_reply_to;
    out.body = with_signature("", account);
    return out;
}

draft reply_to(std::string_view raw, const prefs::account &account,
               const prefs::common &settings, reply_mode mode) {
    const mail::msg_info info = mail::parse_msg_info(raw, 0);
    const mime::part root = mime::parse_message(raw);
    draft out = new_draft(account);
    fill_recipients(out, root.headers, account, mode);
    if (!account.auto_cc.empty())
        out.cc += (out.cc.empty() ? "" : ", ") + account.auto_cc;
    out.subject = reply_subject(info.subject);
    out.in_reply_to = info.message_id.empty()
                          ? std::string()
                          : "<" + info.message_id + ">";
    out.references = references_for(info);
    if (settings.reply_with_quote) {
        const std::string quoted = expand_format(
            settings.reply_format, info, mime::body_text(root, raw),
            settings.quote_mark, account, &out.cursor);
        out.body = with_signature(quoted, account);
        if (out.cursor == 0 || out.cursor > quoted.size())
            out.cursor = quoted.size();
    } else {
        out.cursor = 0;
    }
    return out;
}

draft forward(std::string_view raw, const prefs::account &account,
              const prefs::common &settings) {
    const mail::msg_info info = mail::parse_msg_info(raw, 0);
    const mime::part root = mime::parse_message(raw);
    draft out = new_draft(account);
    out.subject = forward_subject(info.subject);
    const std::string body = expand_format(
        settings.forward_format, info, mime::body_text(root, raw),
        settings.quote_mark, account, &out.cursor);
    out.body = with_signature(body, account);
    out.cursor = 0;
    for (const mime::part *item : mime::attachments(root)) {
        attachment copy;
        copy.name = item->filename.empty() ? "attachment" : item->filename;
        copy.mime_type = item->mime_type();
        copy.data = item->decoded_body(raw);
        out.attachments.push_back(std::move(copy));
    }
    return out;
}

draft reopen(std::string_view file) {
    // A queued message has routing lines ahead of the message itself.
    const queued_message entry = read_queue_entry(file);
    const std::string_view raw = entry.message;
    const mime::part root = mime::parse_message(raw);
    const mime::header_block &headers = root.headers;
    draft out;
    out.account_id = entry.account_id != 0
                         ? entry.account_id
                         : static_cast<int>(
                               util::parse_int(
                                   headers.get("X-Claws-Account-Id"))
                                   .value_or(0));
    out.reply_source = entry.reply_source;
    out.forward_source = entry.forward_source;
    out.from = mime::decode_words(headers.get("From"));
    out.to = mime::decode_words(headers.get("To"));
    out.cc = mime::decode_words(headers.get("Cc"));
    out.bcc = mime::decode_words(headers.get("Bcc"));
    out.reply_to = mime::decode_words(headers.get("Reply-To"));
    out.subject = mime::decode_words(headers.get("Subject"));
    out.in_reply_to = headers.get("In-Reply-To");
    out.references = headers.get("References");
    // Envelope recipients the headers do not name were blind copies.
    for (const std::string &address : entry.recipients)
        if (out.to.find(address) == std::string::npos &&
            out.cc.find(address) == std::string::npos &&
            out.bcc.find(address) == std::string::npos)
            out.bcc += (out.bcc.empty() ? "" : ", ") + address;
    out.body = mime::body_text(root, raw);
    // Rendering ends the body with a line end the writer did not type.
    if (out.body.ends_with('\n'))
        out.body.pop_back();
    for (const mime::part *item : mime::attachments(root)) {
        attachment copy;
        copy.name = item->filename.empty() ? "attachment" : item->filename;
        copy.mime_type = item->mime_type();
        copy.data = item->decoded_body(raw);
        out.attachments.push_back(std::move(copy));
    }
    return out;
}

const prefs::account *account_for_reply(std::string_view raw,
                                        const prefs::account_list &accounts) {
    const mime::header_block headers = mime::header_block::parse(raw);
    for (const char *field : {"To", "Cc", "Delivered-To"})
        for (const mime::mailbox &box :
             mime::parse_addresses(mime::decode_words(headers.get(field))))
            for (const prefs::account &item : accounts.all())
                if (util::iequals(item.address, box.address))
                    return &item;
    return accounts.default_account();
}

std::optional<mime::outgoing_message>
to_outgoing(const draft &text, const prefs::account &account,
            const prefs::common &settings, std::time_t now) {
    mime::outgoing_message message;
    message.from = text.from.empty() ? account.from_header() : text.from;
    message.to = text.to;
    message.cc = text.cc;
    message.bcc = text.bcc;
    message.reply_to = text.reply_to;
    message.subject = text.subject;
    message.body = settings.auto_wrap
                       ? wrap_text(text.body, settings.wrap_length)
                       : text.body;
    message.organization = account.organization;
    message.in_reply_to = text.in_reply_to;
    message.references = text.references;
    message.date = now;
    message.message_id = mime::make_message_id(account.address, now);
    message.user_agent = user_agent_name;
    for (const attachment &item : text.attachments) {
        mime::attachment_data data;
        data.filename = item.name.empty() ? item.path.filename().string()
                                          : item.name;
        data.mime_type = item.mime_type.empty()
                             ? mime::guess_mime_type(data.filename)
                             : item.mime_type;
        if (item.path.empty()) {
            data.data = item.data;
        } else {
            auto bytes = util::read_file(item.path);
            if (!bytes)
                return std::nullopt;
            data.data = std::move(*bytes);
        }
        message.attachments.push_back(std::move(data));
    }
    return message;
}

} // namespace compose
