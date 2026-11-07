//
// RFC 5322 addresses. A real parser replaces the string surgery of
// extract_address and friends, so quoted names, comments and groups
// come apart in one place.
//
// Derived from Claws Mail src/common/utils.c (extract_address,
// address_list_append) and src/procheader.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_ADDRESS_H
#define MIME_ADDRESS_H

#include <string>
#include <string_view>
#include <vector>

namespace mime {

// One mailbox: a display name and an addr-spec.
struct mailbox {
    std::string name;
    std::string address;

    bool operator==(const mailbox &) const = default;
};

//
// Parse an address list.
//
// Parameters:
//      text        - A To, Cc or From value, already unfolded.
//
// Notes:
//      Group syntax contributes its members and loses its name. An item
//      with no @ is kept, since a local alias is a legal thing to type.
//
// Returns:
//      The mailboxes in order; names stay as written, undecoded.
//
std::vector<mailbox> parse_addresses(std::string_view text);

// Return the first mailbox of a list, or an empty one.
mailbox parse_address(std::string_view text);

// Format a mailbox for a header, quoting the name when it needs it.
std::string format_mailbox(const mailbox &box);

// Format a list for a header, comma separated.
std::string format_addresses(const std::vector<mailbox> &boxes);

// Return the name to show: the name if it has one, otherwise the address.
std::string display_name(std::string_view text);

} // namespace mime

#endif
