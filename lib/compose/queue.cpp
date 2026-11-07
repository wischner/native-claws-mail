//
// The queue format.
//
// Derived from Claws Mail src/compose.c and src/procmsg.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "compose/queue.h"

#include "util/text.h"

namespace compose {
namespace {

constexpr std::string_view end_marker = "X-Claws-End-Special-Headers: 1";

} // namespace

std::string write_queue_entry(const queued_message &item) {
    std::string out = "AF:\nNF:0\nPS:10\nSRH:1\nSFN:\nDSR:\nMID:\nCFG:\nPT:0\n";
    out += "S:" + item.sender + "\n";
    out += "RQ:\n";
    out += "SSV:" + item.smtp_server + "\n";
    out += "NSV:\nSSH:\n";
    std::string list;
    for (const std::string &address : item.recipients)
        list += (list.empty() ? "<" : ",<") + address + ">";
    out += "R:" + list + "\n";
    out += "MAID:" + std::to_string(item.account_id) + "\n";
    out += "NAID:0\n";
    out += "SCF:" + item.sent_folder + "\n";
    if (!item.reply_source.empty())
        out += "RMID:" + item.reply_source + "\n";
    if (!item.forward_source.empty())
        out += "FMID:" + item.forward_source + "\n";
    out += end_marker;
    out += "\n";
    return out + item.message;
}

queued_message read_queue_entry(std::string_view text) {
    queued_message item;
    const std::size_t marker = text.find(end_marker);
    if (marker == std::string_view::npos) {
        item.message = text;
        return item;
    }
    for (const std::string &line : util::split_lines(text.substr(0, marker))) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string_view key = std::string_view(line).substr(0, colon);
        const std::string_view value =
            std::string_view(line).substr(colon + 1);
        if (key == "S") {
            item.sender = value;
        } else if (key == "SSV") {
            item.smtp_server = value;
        } else if (key == "MAID") {
            item.account_id =
                static_cast<int>(util::parse_int(value).value_or(0));
        } else if (key == "SCF") {
            item.sent_folder = value;
        } else if (key == "RMID") {
            item.reply_source = value;
        } else if (key == "FMID") {
            item.forward_source = value;
        } else if (key == "R") {
            for (std::string address : util::split(value, ',')) {
                std::string_view word = util::trim(address);
                if (word.starts_with('<') && word.ends_with('>'))
                    word = word.substr(1, word.size() - 2);
                if (!word.empty())
                    item.recipients.emplace_back(word);
            }
        }
    }
    std::size_t start = marker + end_marker.size();
    if (start < text.size() && text[start] == '\r')
        ++start;
    if (start < text.size() && text[start] == '\n')
        ++start;
    item.message = text.substr(start);
    return item;
}

} // namespace compose
