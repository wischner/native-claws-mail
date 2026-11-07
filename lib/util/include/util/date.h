//
// RFC 5322 dates: parsing the many forms mail uses and writing the one
// it should use. Replaces procheader_date_parse and get_rfc822_date.
//
// Derived from Claws Mail src/procheader.c and src/common/utils.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_DATE_H
#define UTIL_DATE_H

#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace util {

//
// Parse a Date header.
//
// Notes:
//      Accepts an optional weekday, two and four digit years, named and
//      numeric zones and missing seconds, as procheader_date_parse did.
//
// Returns:
//      Seconds since the epoch, or nothing when no date can be found.
//
std::optional<std::time_t> parse_mail_date(std::string_view text);

//
// Format a time for a Date header.
//
// Parameters:
//      when        - Seconds since the epoch.
//      offset      - Minutes east of UTC to show.
//
std::string format_mail_date(std::time_t when, int offset);

// Return the local UTC offset in minutes at a time.
int local_utc_offset(std::time_t when);

// Format a time for the message list: "yy/mm/dd(ddd) hh:mm".
std::string format_list_date(std::time_t when);

// Return a time's civil UTC fields.
std::tm utc_fields(std::time_t when);

// Return a time's civil local fields.
std::tm local_fields(std::time_t when);

// Return a time as the epoch from civil UTC fields.
std::time_t civil_to_time(int year, int month, int day, int hour,
                          int minute, int second);

} // namespace util

#endif
