//
// Line buffering and the protocol log.
//
// Derived from Claws Mail src/common/session.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "net/transport.h"

#include "util/log.h"

namespace net {
namespace {

constexpr std::size_t max_line = 8 * 1024 * 1024;

// Return a loggable copy of a line.
std::string_view for_log(std::string_view line) {
    constexpr std::size_t limit = 200;
    return line.substr(0, limit);
}

} // namespace

line_stream::line_stream(std::unique_ptr<transport> connection)
    : connection_(std::move(connection)) {}

bool line_stream::fill() {
    if (position_ > 0) {
        buffer_.erase(0, position_);
        position_ = 0;
    }
    char chunk[16384];
    const std::size_t got = connection_->read(chunk, sizeof chunk);
    if (got == 0)
        return false;
    buffer_.append(chunk, got);
    return true;
}

std::string line_stream::read_line() {
    std::size_t scanned = position_;
    while (true) {
        const std::size_t end = buffer_.find('\n', scanned);
        if (end != std::string::npos) {
            std::size_t stop = end;
            if (stop > position_ && buffer_[stop - 1] == '\r')
                --stop;
            std::string line = buffer_.substr(position_, stop - position_);
            position_ = end + 1;
            util::log_write(util::log_level::received, for_log(line));
            return line;
        }
        if (buffer_.size() - position_ > max_line)
            throw net_error("The server sent an oversized line.");
        const std::size_t held = buffer_.size() - position_;
        if (!fill())
            throw net_error("The server closed the connection.");
        scanned = position_ + held;
    }
}

std::string line_stream::read_bytes(std::size_t size) {
    while (buffer_.size() - position_ < size)
        if (!fill())
            throw net_error("The server closed the connection.");
    std::string out = buffer_.substr(position_, size);
    position_ += size;
    return out;
}

void line_stream::write(std::string_view data) {
    connection_->write(data);
}

void line_stream::write_line(std::string_view line, bool secret) {
    util::log_write(util::log_level::sent,
                    secret ? std::string_view("********") : for_log(line));
    std::string out(line);
    out += "\r\n";
    connection_->write(out);
}

void line_stream::start_tls() {
    connection_->start_tls();
    buffer_.clear();
    position_ = 0;
}

} // namespace net
