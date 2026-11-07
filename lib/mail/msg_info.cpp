//
// Message summaries.
//
// Derived from Claws Mail src/procheader.c and src/common/utils.c
// (subject_get_prefix_length),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/msg_info.h"

#include "mime/address.h"
#include "mime/encoded_word.h"
#include "mime/header.h"
#include "util/date.h"
#include "util/text.h"

namespace mail {
namespace {

// Return message ids in angle brackets found in a value, in order.
std::vector<std::string> message_ids(std::string_view value) {
    std::vector<std::string> ids;
    std::size_t position = 0;
    while (true) {
        const std::size_t open = value.find('<', position);
        if (open == std::string_view::npos)
            return ids;
        const std::size_t close = value.find('>', open);
        if (close == std::string_view::npos)
            return ids;
        ids.emplace_back(value.substr(open + 1, close - open - 1));
        position = close + 1;
    }
}

// Return the length of a reply marker at the start, or zero.
std::size_t reply_prefix(std::string_view text) {
    for (const std::string_view marker :
         {"re", "fw", "fwd", "aw", "sv", "antw", "rif", "tr"}) {
        if (!util::istarts_with(text, marker))
            continue;
        std::size_t at = marker.size();
        // A counter, as in "Re[2]:".
        if (at < text.size() && text[at] == '[') {
            const std::size_t close = text.find(']', at);
            if (close != std::string_view::npos)
                at = close + 1;
        }
        if (at < text.size() && text[at] == ':')
            return at + 1;
    }
    return 0;
}

} // namespace

int msg_info::color_label() const {
    return static_cast<int>((perm_flags >> 7 & 7) | (perm_flags >> 3 & 8));
}

void msg_info::set_color_label(int label) {
    const auto value = static_cast<std::uint32_t>(label);
    perm_flags &= ~flag_clabel_mask;
    perm_flags |= (value & 7) << 7 | (value & 8) << 3;
}

msg_info parse_msg_info(std::string_view raw, std::uint32_t number) {
    const mime::header_block headers = mime::header_block::parse(raw);
    msg_info info;
    info.number = number;
    info.size = raw.size();
    info.date = headers.get("Date");
    if (const auto when = util::parse_mail_date(info.date))
        info.date_time = *when;
    info.from = mime::decode_words(headers.get("From"));
    info.from_name = mime::display_name(info.from);
    info.to = mime::decode_words(headers.get("To"));
    info.cc = mime::decode_words(headers.get("Cc"));
    info.newsgroups = headers.get("Newsgroups");
    info.subject = mime::decode_words(headers.get("Subject"));
    info.xref = headers.get("Xref");
    const auto own = message_ids(headers.get("Message-ID"));
    if (!own.empty())
        info.message_id = own.front();
    const auto parent = message_ids(headers.get("In-Reply-To"));
    if (!parent.empty())
        info.in_reply_to = parent.front();
    info.references = message_ids(headers.get("References"));
    if (info.in_reply_to.empty() && !info.references.empty())
        info.in_reply_to = info.references.back();
    const mime::parameterized_value type =
        mime::parse_parameters(headers.get("Content-Type"));
    if (type.value.starts_with("multipart/")) {
        info.temp_flags |= temp_multipart;
        if (type.value != "multipart/alternative")
            info.temp_flags |= temp_has_attachment;
        if (type.value == "multipart/signed")
            info.temp_flags |= temp_signed;
        if (type.value == "multipart/encrypted")
            info.temp_flags |= temp_encrypted;
    }
    return info;
}

std::string_view strip_subject(std::string_view subject) {
    while (true) {
        std::string_view text = util::trim(subject);
        if (const std::size_t skip = reply_prefix(text); skip != 0) {
            subject = text.substr(skip);
            continue;
        }
        if (text.starts_with('[')) {
            const std::size_t close = text.find(']');
            if (close != std::string_view::npos &&
                close + 1 < text.size()) {
                subject = text.substr(close + 1);
                continue;
            }
        }
        return text;
    }
}

} // namespace mail
