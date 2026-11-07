//
// LDIF and vCard, the formats other programs exchange addresses in.
//
// Derived from Claws Mail src/ldif.c, src/vcard.c and src/exportldif.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "addr/book.h"
#include "util/codec.h"
#include "util/text.h"

namespace addr {
namespace {

// Return lines with folded continuations joined.
std::vector<std::string> unfold(std::string_view text) {
    std::vector<std::string> lines;
    for (std::string &line : util::split_lines(text)) {
        if (!lines.empty() && !line.empty() &&
            (line[0] == ' ' || line[0] == '\t'))
            lines.back() += line.substr(1);
        else
            lines.push_back(std::move(line));
    }
    return lines;
}

// Report whether an LDIF value needs Base64.
bool needs_base64(std::string_view value) {
    if (value.empty())
        return false;
    if (value.front() == ' ' || value.front() == ':' ||
        value.front() == '<' || value.back() == ' ')
        return true;
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 128 || byte == '\n' || byte == '\r' || byte == 0)
            return true;
    }
    return false;
}

// Append one LDIF attribute.
void put_ldif(std::string &out, std::string_view name,
              std::string_view value) {
    if (value.empty())
        return;
    out += name;
    if (needs_base64(value))
        out += ":: " + util::base64_encode(value);
    else
        out += ": " + std::string(value);
    out += '\n';
}

// Finish a person and add it when it has an address.
void finish_person(std::vector<person> &people, person &current) {
    if (!current.emails.empty())
        people.push_back(std::move(current));
    current = person{};
}

} // namespace

std::vector<person> import_ldif(std::string_view text) {
    std::vector<person> people;
    person current;
    for (const std::string &line : unfold(text)) {
        if (util::trim(line).empty()) {
            finish_person(people, current);
            continue;
        }
        if (line.starts_with('#'))
            continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string name = util::lower(line.substr(0, colon));
        const bool encoded =
            colon + 1 < line.size() && line[colon + 1] == ':';
        std::string value(util::trim(
            std::string_view(line).substr(colon + (encoded ? 2 : 1))));
        if (encoded)
            value = util::base64_decode(value);
        if (name == "cn" || name == "commonname")
            current.common_name = value;
        else if (name == "givenname")
            current.first_name = value;
        else if (name == "sn" || name == "surname")
            current.last_name = value;
        else if (name == "mail" || name == "mozillasecondemail")
            current.emails.push_back({"", "", value, ""});
        else if (name == "mozillanickname")
            current.nick_name = value;
    }
    finish_person(people, current);
    return people;
}

std::vector<person> import_vcard(std::string_view text) {
    std::vector<person> people;
    person current;
    for (const std::string &line : unfold(text)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string head = util::upper(line.substr(0, colon));
        const std::string value(
            util::trim(std::string_view(line).substr(colon + 1)));
        const std::string field = head.substr(0, head.find(';'));
        if (head == "BEGIN") {
            current = person{};
        } else if (head == "END") {
            finish_person(people, current);
        } else if (field == "FN") {
            current.common_name = value;
        } else if (field == "N") {
            const auto parts = util::split(value, ';');
            current.last_name = parts[0];
            if (parts.size() > 1)
                current.first_name = parts[1];
        } else if (field == "NICKNAME") {
            current.nick_name = value;
        } else if (field == "EMAIL") {
            current.emails.push_back({"", "", value, ""});
        }
    }
    return people;
}

std::string export_ldif(const std::vector<person> &people) {
    std::string out;
    for (const person &item : people) {
        for (const email_entry &email : item.emails) {
            put_ldif(out, "dn",
                     "cn=" + item.display_name() + ",mail=" + email.address);
            out += "objectClass: top\nobjectClass: person\n"
                   "objectClass: inetOrgPerson\n";
            put_ldif(out, "cn", item.display_name());
            put_ldif(out, "givenName", item.first_name);
            put_ldif(out, "sn", item.last_name);
            put_ldif(out, "mail", email.address);
            out += '\n';
        }
    }
    return out;
}

} // namespace addr
