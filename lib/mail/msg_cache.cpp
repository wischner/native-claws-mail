//
// The .claws_cache and .claws_mark formats.
//
// Derived from Claws Mail src/msgcache.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/msg_cache.h"

#include "util/charset.h"
#include "util/files.h"

namespace mail {
namespace {

constexpr std::uint32_t cache_version = 24;
constexpr std::uint32_t mark_version = 2;
constexpr const char *cache_name = ".claws_cache";
constexpr const char *mark_name = ".claws_mark";
constexpr std::uint32_t max_string = 0x7FFFFFFF;

// Reads little endian fields with bounds checks.
class reader {
public:
    explicit reader(std::string_view data) : data_(data) {}

    bool at_end() const { return position_ >= data_.size(); }

    // Read one 32 bit value; returns false past the end.
    bool read(std::uint32_t &value) {
        if (data_.size() - position_ < 4)
            return false;
        value = 0;
        for (int i = 3; i >= 0; --i)
            value = value << 8 |
                    static_cast<unsigned char>(
                        data_[position_ + static_cast<std::size_t>(i)]);
        position_ += 4;
        return true;
    }

    // Read one length-prefixed string.
    bool read(std::string &value) {
        std::uint32_t length = 0;
        if (!read(length) || length > max_string ||
            data_.size() - position_ < length)
            return false;
        value.assign(data_.substr(position_, length));
        position_ += length;
        return true;
    }

private:
    std::string_view data_;
    std::size_t position_ = 0;
};

// Append one little endian 32 bit value.
void write_u32(std::string &out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out += static_cast<char>(value >> (i * 8) & 255);
}

// Append one length-prefixed string.
void write_string(std::string &out, std::string_view value) {
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out += value;
}

// Read one record; returns false when the data is truncated.
bool read_record(reader &in, msg_info &info) {
    std::uint32_t size = 0, mtime = 0, date = 0, references = 0;
    std::uint32_t planned = 0, total = 0;
    const bool fields =
        in.read(info.number) && in.read(size) && in.read(mtime) &&
        in.read(date) && in.read(info.temp_flags) && in.read(info.from_name) &&
        in.read(info.date) && in.read(info.from) && in.read(info.to) &&
        in.read(info.cc) && in.read(info.newsgroups) &&
        in.read(info.subject) && in.read(info.message_id) &&
        in.read(info.in_reply_to) && in.read(info.xref) &&
        in.read(planned) && in.read(total) && in.read(references);
    if (!fields)
        return false;
    info.size = size;
    info.mtime = mtime;
    info.date_time = date;
    info.planned_download = static_cast<std::int32_t>(planned);
    info.total_size = static_cast<std::int32_t>(total);
    for (std::uint32_t i = 0; i < references; ++i) {
        std::string reference;
        if (!in.read(reference))
            return false;
        if (!reference.empty())
            info.references.push_back(std::move(reference));
    }
    return true;
}

// Append one record.
void write_record(std::string &out, const msg_info &info) {
    write_u32(out, info.number);
    write_u32(out, static_cast<std::uint32_t>(info.size));
    write_u32(out, static_cast<std::uint32_t>(info.mtime));
    write_u32(out, static_cast<std::uint32_t>(info.date_time));
    write_u32(out, info.temp_flags);
    for (const std::string *field :
         {&info.from_name, &info.date, &info.from, &info.to, &info.cc,
          &info.newsgroups, &info.subject, &info.message_id,
          &info.in_reply_to, &info.xref})
        write_string(out, *field);
    write_u32(out, static_cast<std::uint32_t>(info.planned_download));
    write_u32(out, static_cast<std::uint32_t>(info.total_size));
    write_u32(out, static_cast<std::uint32_t>(info.references.size()));
    for (const std::string &reference : info.references)
        write_string(out, reference);
}

// Apply the mark file to a loaded cache.
void read_marks(const std::filesystem::path &directory, msg_map &messages) {
    const auto data = util::read_file(directory / mark_name);
    if (!data)
        return;
    reader in(*data);
    std::uint32_t version = 0;
    if (!in.read(version) || version != mark_version)
        return;
    std::uint32_t number = 0, flags = 0;
    while (in.read(number) && in.read(flags))
        if (const auto it = messages.find(number); it != messages.end())
            it->second.perm_flags = flags;
}

} // namespace

msg_map read_msg_cache(const std::filesystem::path &directory) {
    msg_map messages;
    const auto data = util::read_file(directory / cache_name);
    if (!data)
        return messages;
    reader in(*data);
    std::uint32_t version = 0;
    std::string charset;
    if (!in.read(version) || version != cache_version || !in.read(charset))
        return messages;
    while (!in.at_end()) {
        msg_info info;
        if (!read_record(in, info))
            return {};
        if (!charset.empty() && charset != "UTF-8") {
            info.subject = util::to_utf8(info.subject, charset);
            info.from = util::to_utf8(info.from, charset);
            info.from_name = util::to_utf8(info.from_name, charset);
        }
        messages.emplace(info.number, std::move(info));
    }
    read_marks(directory, messages);
    return messages;
}

bool write_msg_cache(const std::filesystem::path &directory,
                     const msg_map &messages) {
    std::string cache;
    std::string marks;
    write_u32(cache, cache_version);
    write_string(cache, "UTF-8");
    write_u32(marks, mark_version);
    for (const auto &[number, info] : messages) {
        write_record(cache, info);
        write_u32(marks, number);
        write_u32(marks, info.perm_flags);
    }
    const bool wrote_cache = util::write_file(directory / cache_name, cache);
    return util::write_file(directory / mark_name, marks) && wrote_cache;
}

} // namespace mail
