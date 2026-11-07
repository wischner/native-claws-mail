//
// RFC 5322 date parsing and formatting.
//
// Derived from Claws Mail src/procheader.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "util/date.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <vector>

#include "util/text.h"

namespace util {
namespace {

constexpr std::array<const char *, 12> months{
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"};
constexpr std::array<const char *, 7> day_names{"Sun", "Mon", "Tue", "Wed",
                                                "Thu", "Fri", "Sat"};
constexpr std::array<const char *, 12> month_names{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// A named zone and its offset in minutes.
struct named_zone {
    const char *name;
    int offset;
};

constexpr std::array<named_zone, 11> zones{
    {{"ut", 0},
     {"utc", 0},
     {"gmt", 0},
     {"est", -300},
     {"edt", -240},
     {"cst", -360},
     {"cdt", -300},
     {"mst", -420},
     {"mdt", -360},
     {"pst", -480},
     {"pdt", -420}}};

// Return days since the epoch of a civil date.
std::int64_t days_from_civil(int year, int month, int day) {
    const std::int64_t y = month <= 2 ? year - 1 : year;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Split a date into tokens at spaces, commas and comment parentheses.
std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    int depth = 0;
    for (const char c : text) {
        if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0) {
            --depth;
            continue;
        }
        const bool separator =
            depth > 0 || c == ' ' || c == '\t' || c == ',' || c == '\r' ||
            c == '\n';
        if (!separator) {
            current += c;
            continue;
        }
        if (!current.empty())
            tokens.push_back(std::move(current));
        current.clear();
    }
    if (!current.empty())
        tokens.push_back(std::move(current));
    return tokens;
}

// Parse hh:mm or hh:mm:ss.
bool parse_clock(const std::string &token, int &hour, int &minute,
                 int &second) {
    const auto parts = split(token, ':');
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    const auto h = parse_int(parts[0]);
    const auto m = parse_int(parts[1]);
    const auto s = parts.size() == 3 ? parse_int(parts[2])
                                     : std::optional<std::int64_t>(0);
    if (!h || !m || !s)
        return false;
    hour = static_cast<int>(*h);
    minute = static_cast<int>(*m);
    second = static_cast<int>(*s);
    return true;
}

// Return a month from its name, or zero.
int parse_month(const std::string &token) {
    for (std::size_t i = 0; i < months.size(); ++i)
        if (istarts_with(token, months[i]))
            return static_cast<int>(i) + 1;
    return 0;
}

// Parse a numeric or named zone into minutes east of UTC.
bool parse_zone(const std::string &token, int &offset) {
    if ((token[0] == '+' || token[0] == '-') && token.size() >= 3) {
        const auto value = parse_int(token.substr(1));
        if (!value)
            return false;
        const int minutes = token.size() <= 3
                                ? static_cast<int>(*value) * 60
                                : static_cast<int>(*value / 100 * 60 +
                                                   *value % 100);
        offset = token[0] == '-' ? -minutes : minutes;
        return true;
    }
    for (const named_zone &zone : zones)
        if (iequals(token, zone.name)) {
            offset = zone.offset;
            return true;
        }
    return false;
}

} // namespace

std::tm utc_fields(std::time_t when) {
    std::tm fields{};
#ifdef _WIN32
    gmtime_s(&fields, &when);
#else
    gmtime_r(&when, &fields);
#endif
    return fields;
}

std::tm local_fields(std::time_t when) {
    std::tm fields{};
#ifdef _WIN32
    localtime_s(&fields, &when);
#else
    localtime_r(&when, &fields);
#endif
    return fields;
}

std::time_t civil_to_time(int year, int month, int day, int hour,
                          int minute, int second) {
    return static_cast<std::time_t>(days_from_civil(year, month, day) *
                                        86400 +
                                    hour * 3600 + minute * 60 + second);
}

std::optional<std::time_t> parse_mail_date(std::string_view text) {
    int year = -1, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int offset = 0;
    bool have_clock = false;
    for (const std::string &token : tokenize(text)) {
        if (!have_clock && token.find(':') != std::string::npos) {
            have_clock = parse_clock(token, hour, minute, second);
            continue;
        }
        if (month == 0)
            if (const int found = parse_month(token); found != 0) {
                month = found;
                continue;
            }
        if (std::isdigit(static_cast<unsigned char>(token[0]))) {
            const auto number = parse_int(token);
            if (!number)
                continue;
            if (day == 0 && *number >= 1 && *number <= 31 &&
                token.size() <= 2 && month == 0) {
                day = static_cast<int>(*number);
                continue;
            }
            if (day == 0 && month != 0 && token.size() <= 2 &&
                *number >= 1 && *number <= 31) {
                day = static_cast<int>(*number);
                continue;
            }
            if (year < 0) {
                year = static_cast<int>(*number);
                if (token.size() <= 2)
                    year += year < 70 ? 2000 : 1900;
                else if (token.size() == 3)
                    year += 1900;
            }
            continue;
        }
        parse_zone(token, offset);
    }
    if (year < 0 || month == 0 || day == 0)
        return std::nullopt;
    return civil_to_time(year, month, day, hour, minute, second) -
           offset * 60;
}

std::string format_mail_date(std::time_t when, int offset) {
    const std::tm fields = utc_fields(when + offset * 60);
    const int magnitude = offset < 0 ? -offset : offset;
    char buffer[64];
    std::snprintf(buffer, sizeof buffer,
                  "%s, %d %s %04d %02d:%02d:%02d %c%02d%02d",
                  day_names[static_cast<std::size_t>(fields.tm_wday)],
                  fields.tm_mday,
                  month_names[static_cast<std::size_t>(fields.tm_mon)],
                  fields.tm_year + 1900, fields.tm_hour, fields.tm_min,
                  fields.tm_sec, offset < 0 ? '-' : '+', magnitude / 60,
                  magnitude % 60);
    return buffer;
}

int local_utc_offset(std::time_t when) {
    const std::tm local = local_fields(when);
    const std::time_t as_utc =
        civil_to_time(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
    return static_cast<int>((as_utc - when) / 60);
}

std::string format_list_date(std::time_t when) {
    const std::tm local = local_fields(when);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%y/%m/%d(%a) %H:%M", &local);
    return buffer;
}

} // namespace util
