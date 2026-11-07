//
// What is known about a message without opening it. MsgInfo's flags are
// kept bit for bit, because they are stored in Claws' mark files.
//
// Derived from Claws Mail src/procmsg.h, src/procheader.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_MSG_INFO_H
#define MAIL_MSG_INFO_H

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace mail {

// Permanent flags, as stored in .claws_mark.
inline constexpr std::uint32_t flag_new = 1U << 0;
inline constexpr std::uint32_t flag_unread = 1U << 1;
inline constexpr std::uint32_t flag_marked = 1U << 2;
inline constexpr std::uint32_t flag_deleted = 1U << 3;
inline constexpr std::uint32_t flag_replied = 1U << 4;
inline constexpr std::uint32_t flag_forwarded = 1U << 5;
inline constexpr std::uint32_t flag_clabel_mask = 15U << 6;
inline constexpr std::uint32_t flag_ignore_thread = 1U << 10;
inline constexpr std::uint32_t flag_locked = 1U << 11;
inline constexpr std::uint32_t flag_spam = 1U << 13;
inline constexpr std::uint32_t flag_watch_thread = 1U << 15;

// Temporary flags, as stored in .claws_cache.
inline constexpr std::uint32_t temp_queued = 1U << 16;
inline constexpr std::uint32_t temp_draft = 1U << 17;
inline constexpr std::uint32_t temp_encrypted = 1U << 18;
inline constexpr std::uint32_t temp_signed = 1U << 21;
inline constexpr std::uint32_t temp_multipart = 1U << 29;
inline constexpr std::uint32_t temp_has_attachment = 1U << 30;
inline constexpr std::uint32_t temp_scanned = 1U << 31;

// A message's summary. Strings are UTF-8, decoded.
struct msg_info {
    std::uint32_t number = 0;
    std::uint64_t size = 0;
    std::time_t mtime = 0;
    std::time_t date_time = 0;
    std::uint32_t perm_flags = 0;
    std::uint32_t temp_flags = 0;
    std::string from_name;
    std::string date;
    std::string from;
    std::string to;
    std::string cc;
    std::string newsgroups;
    std::string subject;
    std::string message_id;
    std::string in_reply_to;
    std::string xref;
    std::vector<std::string> references;
    std::int32_t planned_download = 0;
    std::int32_t total_size = 0;

    // Report whether a permanent flag is set.
    bool has(std::uint32_t flag) const { return (perm_flags & flag) != 0; }

    // Return the color label, 0 to 15.
    int color_label() const;

    // Set the color label.
    void set_color_label(int label);
};

//
// Build a summary from raw message text.
//
// Parameters:
//      raw         - The message, or at least its headers.
//      number      - The message's number in its folder.
//
// Notes:
//      The new message flags are the caller's: this sets none of them.
//      Multipart and attachment state is read from the Content-Type so
//      the list can show a paper clip without parsing bodies.
//
msg_info parse_msg_info(std::string_view raw, std::uint32_t number);

// Return the subject without "Re:", "Fwd:" and "[list]" prefixes.
std::string_view strip_subject(std::string_view subject);

} // namespace mail

#endif
