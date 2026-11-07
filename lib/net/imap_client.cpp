//
// The IMAP wire protocol: a response parser, and a thin method for each
// verb. Every string sent to the server goes through literal_or_quoted,
// so a mailbox name cannot smuggle in a second command.
//
// Derived from Claws Mail src/imap.c and src/etpan/imap-thread.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "net/imap_client.h"

#include <algorithm>
#include <cctype>

#include "util/codec.h"
#include "util/text.h"

namespace net {
namespace {

// Report whether a word is a status response.
bool is_status(std::string_view word) {
    return word == "OK" || word == "NO" || word == "BAD" || word == "BYE" ||
           word == "PREAUTH";
}

// Walks one line of tokens, asking for literals as it meets them.
class item_parser {
public:
    item_parser(std::string line,
                const std::function<std::string(std::size_t)> &read_literal,
                const std::function<std::string()> &read_line)
        : line_(std::move(line)), read_literal_(read_literal),
          read_line_(read_line) {}

    // Parse everything to the end of the logical line.
    void run(std::vector<imap_item> &into) { parse_sequence(into, false); }

private:
    void skip_spaces() {
        while (position_ < line_.size() && line_[position_] == ' ')
            ++position_;
    }

    void parse_sequence(std::vector<imap_item> &into, bool nested) {
        while (true) {
            skip_spaces();
            if (position_ >= line_.size())
                return;
            const char c = line_[position_];
            if (c == ')') {
                ++position_;
                if (nested)
                    return;
                continue;
            }
            if (c == '(') {
                ++position_;
                imap_item list;
                list.type = imap_item::kind::list;
                if (++depth_ > 64)
                    throw net_error("Malformed response from the server.");
                parse_sequence(list.children, true);
                --depth_;
                into.push_back(std::move(list));
            } else if (c == '"') {
                into.push_back(parse_quoted());
            } else if (c == '{') {
                into.push_back(parse_literal());
            } else {
                into.push_back(parse_atom());
            }
        }
    }

    imap_item parse_quoted() {
        imap_item item;
        item.type = imap_item::kind::string;
        ++position_;
        while (position_ < line_.size() && line_[position_] != '"') {
            if (line_[position_] == '\\' && position_ + 1 < line_.size())
                ++position_;
            item.text += line_[position_++];
        }
        ++position_;
        return item;
    }

    imap_item parse_literal() {
        const std::size_t close = line_.find('}', position_);
        if (close == std::string::npos)
            throw net_error("Malformed literal from the server.");
        const auto size = util::parse_int(std::string_view(line_).substr(
            position_ + 1, close - position_ - 1));
        constexpr std::int64_t limit = std::int64_t{1} << 30;
        if (!size || *size < 0 || *size > limit)
            throw net_error("Malformed literal from the server.");
        imap_item item;
        item.type = imap_item::kind::string;
        item.text = read_literal_(static_cast<std::size_t>(*size));
        line_ = read_line_();
        position_ = 0;
        return item;
    }

    imap_item parse_atom() {
        imap_item item;
        int brackets = 0;
        while (position_ < line_.size()) {
            const char c = line_[position_];
            if (c == '[')
                ++brackets;
            else if (c == ']' && brackets > 0)
                --brackets;
            else if (brackets == 0 && (c == ' ' || c == '(' || c == ')'))
                break;
            item.text += c;
            ++position_;
        }
        if (item.text == "NIL")
            item.type = imap_item::kind::nil;
        return item;
    }

    std::string line_;
    std::size_t position_ = 0;
    int depth_ = 0;
    const std::function<std::string(std::size_t)> &read_literal_;
    const std::function<std::string()> &read_line_;
};

// Return the uppercase text of an item.
std::string upper_text(const imap_item &item) {
    return util::upper(item.text);
}

// Read a number out of an item.
std::uint64_t number_of(const imap_item &item) {
    return static_cast<std::uint64_t>(
        util::parse_int(item.text).value_or(0));
}

// Read a FETCH attribute list into a message.
imap_message parse_fetch(const imap_item &list) {
    imap_message message;
    for (std::size_t i = 0; i + 1 < list.children.size(); i += 2) {
        const std::string key = upper_text(list.children[i]);
        const imap_item &value = list.children[i + 1];
        if (key == "UID") {
            message.uid = static_cast<std::uint32_t>(number_of(value));
        } else if (key == "RFC822.SIZE") {
            message.size = number_of(value);
        } else if (key == "FLAGS") {
            for (const imap_item &flag : value.children)
                message.flags.insert(flag.text);
        } else if (key == "RFC822.HEADER" || key == "BODY[HEADER]") {
            message.headers = value.text;
        } else if (key == "BODY[]" || key == "RFC822") {
            message.body = value.text;
        }
    }
    return message;
}

// Return the value of a word within a status code, as in "UIDNEXT 5".
std::optional<std::uint32_t> code_number(std::string_view code,
                                         std::string_view word) {
    const auto parts = util::split(code, ' ');
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        if (util::iequals(parts[i], word))
            if (const auto value = util::parse_int(parts[i + 1]))
                return static_cast<std::uint32_t>(*value);
    return std::nullopt;
}

} // namespace

imap_client::imap_client(const endpoint &target, const connector &connect)
    : stream_(connect(target)) {
    const response greeting = read_response();
    if (greeting.status == "BYE" || greeting.status.empty())
        throw net_error("IMAP server refused the connection: " +
                        greeting.text);
    authenticated_ = greeting.status == "PREAUTH";
    if (!greeting.code.empty())
        note_capabilities(greeting);
    if (target.tls == tls_mode::starttls) {
        read_capabilities();
        if (!capabilities_.contains("STARTTLS"))
            throw net_error("The IMAP server does not offer STARTTLS.");
        require_ok(run("STARTTLS"), "STARTTLS");
        stream_.start_tls();
        capabilities_.clear(); // Learned before TLS; not to be trusted.
    }
    if (capabilities_.empty())
        read_capabilities();
}

std::string imap_client::next_tag() {
    return "b" + std::to_string(++tag_serial_);
}

void imap_client::parse_items(std::string line,
                              std::vector<imap_item> &into) {
    const std::function<std::string(std::size_t)> read_literal =
        [this](std::size_t size) { return stream_.read_bytes(size); };
    const std::function<std::string()> read_line = [this] {
        return stream_.read_line();
    };
    item_parser(std::move(line), read_literal, read_line).run(into);
}

imap_client::response imap_client::read_response() {
    const std::string line = stream_.read_line();
    response result;
    const std::size_t space = line.find(' ');
    result.tag = line.substr(0, space);
    if (space == std::string::npos)
        return result;
    std::string_view rest = std::string_view(line).substr(space + 1);
    const std::size_t word_end = rest.find(' ');
    const std::string word(rest.substr(0, word_end));
    if (is_status(util::upper(word))) {
        result.status = util::upper(word);
        std::string_view text = word_end == std::string_view::npos
                                    ? std::string_view()
                                    : rest.substr(word_end + 1);
        if (text.starts_with('[')) {
            const std::size_t close = text.find(']');
            if (close != std::string_view::npos) {
                result.code = text.substr(1, close - 1);
                text = util::trim(text.substr(close + 1));
            }
        }
        result.text = text;
        return result;
    }
    parse_items(std::string(rest), result.items);
    return result;
}

imap_client::response
imap_client::finish(const std::string &tag,
                    const untagged_handler &untagged) {
    while (true) {
        response item = read_response();
        if (item.tag == tag)
            return item;
        if (item.tag == "*") {
            if (item.status == "BYE")
                throw net_error("The IMAP server closed the session: " +
                                item.text);
            if (untagged)
                untagged(item);
        }
    }
}

imap_client::response imap_client::run(const std::string &command,
                                       bool secret,
                                       const untagged_handler &untagged) {
    const std::string tag = next_tag();
    stream_.write_line(tag + " " + command, secret);
    return finish(tag, untagged);
}

void imap_client::require_ok(const response &result,
                             std::string_view what) const {
    if (result.status != "OK")
        throw net_error("IMAP " + std::string(what) + " failed: " +
                        result.text);
}

void imap_client::note_capabilities(const response &item) {
    if (!util::istarts_with(item.code, "CAPABILITY "))
        return;
    capabilities_.clear();
    for (const std::string &word :
         util::split(std::string_view(item.code).substr(11), ' '))
        capabilities_.insert(util::upper(word));
}

void imap_client::read_capabilities() {
    capabilities_.clear();
    const response result = run("CAPABILITY", false, [&](const response &r) {
        if (r.items.empty() || upper_text(r.items[0]) != "CAPABILITY")
            return;
        for (std::size_t i = 1; i < r.items.size(); ++i)
            capabilities_.insert(upper_text(r.items[i]));
    });
    require_ok(result, "CAPABILITY");
}

std::string imap_client::literal_or_quoted(std::string_view text) {
    if (text.find_first_of("\r\n") == std::string_view::npos)
        return imap_quote(text);
    throw net_error("A name contains an illegal character.");
}

void imap_client::login(const std::string &user,
                        const std::string &password) {
    response result;
    if (capabilities_.contains("AUTH=CRAM-MD5")) {
        const std::string tag = next_tag();
        stream_.write_line(tag + " AUTHENTICATE CRAM-MD5");
        const response challenge = read_response();
        if (challenge.tag != "+" || challenge.items.empty()) {
            result = challenge;
        } else {
            const std::string answer =
                user + " " +
                util::hmac_md5_hex(
                    password, util::base64_decode(challenge.items[0].text));
            stream_.write_line(util::base64_encode(answer), true);
            result = finish(tag, {});
        }
    } else if (!capabilities_.contains("LOGINDISABLED")) {
        result = run("LOGIN " + literal_or_quoted(user) + " " +
                         literal_or_quoted(password),
                     true);
    } else {
        throw auth_error("The IMAP server does not accept this login.");
    }
    if (result.status != "OK")
        throw auth_error("IMAP login failed: " + result.text);
    authenticated_ = true;
    if (!result.code.empty())
        note_capabilities(result);
    read_capabilities();
}

std::vector<imap_mailbox> imap_client::list(std::string_view reference,
                                            std::string_view pattern,
                                            bool subscribed_only) {
    std::vector<imap_mailbox> boxes;
    const char *verb = subscribed_only ? "LSUB" : "LIST";
    const response result = run(
        std::string(verb) + " " + literal_or_quoted(reference) + " " +
            literal_or_quoted(pattern),
        false, [&](const response &item) {
            if (item.items.size() < 4 || upper_text(item.items[0]) != verb)
                return;
            imap_mailbox box;
            for (const imap_item &flag : item.items[1].children) {
                const std::string name = util::upper(flag.text);
                box.no_select |= name == "\\NOSELECT";
                box.no_inferiors |= name == "\\NOINFERIORS";
            }
            if (!item.items[2].text.empty())
                box.delimiter = item.items[2].text[0];
            box.raw_name = item.items[3].text;
            box.name = imap_utf7_decode(box.raw_name);
            boxes.push_back(std::move(box));
        });
    require_ok(result, verb);
    return boxes;
}

void imap_client::parse_selection(const response &item,
                                  imap_selection &selection) {
    if (const auto value = code_number(item.code, "UIDVALIDITY"))
        selection.uid_validity = *value;
    if (const auto value = code_number(item.code, "UIDNEXT"))
        selection.uid_next = *value;
    if (item.items.size() >= 2 && upper_text(item.items[1]) == "EXISTS")
        selection.exists = static_cast<std::uint32_t>(number_of(item.items[0]));
}

imap_selection imap_client::select(std::string_view raw_name) {
    imap_selection selection;
    const response result = run(
        "SELECT " + literal_or_quoted(raw_name), false,
        [&](const response &item) { parse_selection(item, selection); });
    require_ok(result, "SELECT");
    selection.read_only =
        util::istarts_with(result.code, "READ-ONLY");
    return selection;
}

std::vector<std::uint32_t> imap_client::search_all() {
    std::vector<std::uint32_t> uids;
    const response result =
        run("UID SEARCH ALL", false, [&](const response &item) {
            if (item.items.empty() || upper_text(item.items[0]) != "SEARCH")
                return;
            for (std::size_t i = 1; i < item.items.size(); ++i)
                uids.push_back(
                    static_cast<std::uint32_t>(number_of(item.items[i])));
        });
    require_ok(result, "SEARCH");
    std::ranges::sort(uids);
    return uids;
}

std::vector<imap_message> imap_client::fetch_summaries(std::string_view uids,
                                                       bool headers) {
    std::vector<imap_message> messages;
    std::string command = "UID FETCH " + std::string(uids) +
                          " (UID FLAGS RFC822.SIZE";
    command += headers ? " BODY.PEEK[HEADER])" : ")";
    const response result = run(command, false, [&](const response &item) {
        if (item.items.size() >= 3 && upper_text(item.items[1]) == "FETCH" &&
            item.items[2].type == imap_item::kind::list)
            messages.push_back(parse_fetch(item.items[2]));
    });
    require_ok(result, "FETCH");
    return messages;
}

std::optional<std::string> imap_client::fetch_message(std::uint32_t uid) {
    std::optional<std::string> body;
    const response result =
        run("UID FETCH " + std::to_string(uid) + " (UID BODY.PEEK[])", false,
            [&](const response &item) {
                if (item.items.size() < 3 ||
                    upper_text(item.items[1]) != "FETCH")
                    return;
                imap_message message = parse_fetch(item.items[2]);
                if (message.uid == uid)
                    body = std::move(message.body);
            });
    require_ok(result, "FETCH");
    return body;
}

void imap_client::store_flags(std::uint32_t uid, bool add,
                              std::string_view flags) {
    require_ok(run("UID STORE " + std::to_string(uid) +
                   (add ? " +FLAGS.SILENT (" : " -FLAGS.SILENT (") +
                   std::string(flags) + ")"),
               "STORE");
}

std::uint32_t imap_client::copy(std::uint32_t uid,
                                std::string_view raw_name) {
    const response result =
        run("UID COPY " + std::to_string(uid) + " " +
            literal_or_quoted(raw_name));
    require_ok(result, "COPY");
    // [COPYUID validity source destination]
    const auto parts = util::split(result.code, ' ');
    if (parts.size() == 4 && util::iequals(parts[0], "COPYUID"))
        return static_cast<std::uint32_t>(
            util::parse_int(parts[3]).value_or(0));
    return 0;
}

std::uint32_t imap_client::append(std::string_view raw_name,
                                  std::string_view message,
                                  std::string_view flags) {
    const std::string body = util::to_crlf(message);
    const std::string tag = next_tag();
    stream_.write_line(tag + " APPEND " + literal_or_quoted(raw_name) +
                       " (" + std::string(flags) + ") {" +
                       std::to_string(body.size()) + "}");
    const response prompt = read_response();
    if (prompt.tag != "+") {
        if (prompt.tag == tag)
            require_ok(prompt, "APPEND");
        throw net_error("IMAP APPEND was refused.");
    }
    stream_.write(body);
    stream_.write("\r\n");
    const response result = finish(tag, {});
    require_ok(result, "APPEND");
    const auto parts = util::split(result.code, ' ');
    if (parts.size() == 3 && util::iequals(parts[0], "APPENDUID"))
        return static_cast<std::uint32_t>(
            util::parse_int(parts[2]).value_or(0));
    return 0;
}

void imap_client::expunge(std::uint32_t uid) {
    if (capabilities_.contains("UIDPLUS"))
        require_ok(run("UID EXPUNGE " + std::to_string(uid)), "EXPUNGE");
    else
        require_ok(run("EXPUNGE"), "EXPUNGE");
}

void imap_client::create(std::string_view raw_name) {
    require_ok(run("CREATE " + literal_or_quoted(raw_name)), "CREATE");
}

void imap_client::remove(std::string_view raw_name) {
    require_ok(run("DELETE " + literal_or_quoted(raw_name)), "DELETE");
}

void imap_client::rename(std::string_view from, std::string_view to) {
    require_ok(run("RENAME " + literal_or_quoted(from) + " " +
                   literal_or_quoted(to)),
               "RENAME");
}

void imap_client::noop() { require_ok(run("NOOP"), "NOOP"); }

void imap_client::logout() {
    try {
        run("LOGOUT");
    } catch (const net_error &) {
        // The session is ending either way.
    }
}

} // namespace net
