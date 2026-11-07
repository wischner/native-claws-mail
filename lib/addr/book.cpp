//
// Address books.
//
// Derived from Claws Mail src/addrbook.c, src/addritem.c and
// src/addr_compl.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "addr/book.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "mime/address.h"
#include "util/files.h"
#include "util/text.h"
#include "util/xml_reader.h"

namespace addr {
namespace {

namespace fs = std::filesystem;

constexpr const char *index_name = "addrbook--index.xml";

// Return an attribute, or empty.
std::string get(const util::xml_attributes &attributes, std::string_view key) {
    const auto it = attributes.find(key);
    return it == attributes.end() ? std::string() : it->second;
}

// Reads one book.
class book_reader final : public util::xml_handler {
public:
    explicit book_reader(book &target) : book_(target) {}

    std::string name;

    void start_element(std::string_view element,
                       const util::xml_attributes &a) override {
        if (element == "address-book") {
            name = get(a, "name");
        } else if (element == "person") {
            person_ = &book_.people().emplace_back();
            person_->uid = get(a, "uid");
            person_->first_name = get(a, "first-name");
            person_->last_name = get(a, "last-name");
            person_->nick_name = get(a, "nick-name");
            person_->common_name = get(a, "cn");
        } else if (element == "address" && person_ != nullptr) {
            person_->emails.push_back({get(a, "uid"), get(a, "alias"),
                                       get(a, "email"), get(a, "remarks")});
        } else if (element == "attribute" && person_ != nullptr) {
            attribute_ =
                &person_->attributes.emplace_back(
                    attribute{get(a, "uid"), get(a, "name"), ""});
        } else if (element == "group") {
            group_ = &book_.groups().emplace_back();
            group_->uid = get(a, "uid");
            group_->name = get(a, "name");
            group_->remarks = get(a, "remarks");
        } else if (element == "member" && group_ != nullptr) {
            group_->members.push_back({get(a, "pid"), get(a, "eid")});
        }
    }

    void end_element(std::string_view element) override {
        if (element == "person")
            person_ = nullptr;
        else if (element == "group")
            group_ = nullptr;
        else if (element == "attribute")
            attribute_ = nullptr;
    }

    void characters(std::string_view text) override {
        if (attribute_ != nullptr)
            attribute_->value += text;
    }

private:
    book &book_;
    person *person_ = nullptr;
    group *group_ = nullptr;
    attribute *attribute_ = nullptr;
};

// Reads the index of books.
class index_reader final : public util::xml_handler {
public:
    std::vector<std::pair<std::string, std::string>> entries;

    void start_element(std::string_view element,
                       const util::xml_attributes &a) override {
        if (element == "book")
            entries.emplace_back(get(a, "name"), get(a, "file"));
    }
    void end_element(std::string_view) override {}
    void characters(std::string_view) override {}
};

// Append an attribute.
void put(std::string &out, const char *name, std::string_view value) {
    out += ' ';
    out += name;
    out += "=\"";
    out += util::xml_escape(value);
    out += '"';
}

// Return whether a word of a name starts with a prefix.
bool any_word_starts(std::string_view text, std::string_view prefix) {
    std::size_t start = 0;
    while (start <= text.size()) {
        if (util::istarts_with(text.substr(start), prefix))
            return true;
        const std::size_t next = text.find(' ', start);
        if (next == std::string_view::npos)
            return false;
        start = next + 1;
    }
    return false;
}

// Return the numeric part of a uid, or zero.
std::uint64_t uid_number(const std::string &uid) {
    return static_cast<std::uint64_t>(util::parse_int(uid).value_or(0));
}

} // namespace

std::string person::display_name() const {
    if (!common_name.empty())
        return common_name;
    std::string joined = first_name;
    if (!last_name.empty())
        joined += (joined.empty() ? "" : " ") + last_name;
    if (!joined.empty())
        return joined;
    if (!nick_name.empty())
        return nick_name;
    return emails.empty() ? std::string() : emails.front().address;
}

std::string completion::formatted() const {
    return mime::format_mailbox({name, address});
}

bool book::load(const fs::path &path) {
    const auto text = util::read_file(path);
    if (!text)
        return false;
    people_.clear();
    groups_.clear();
    book_reader reader(*this);
    std::string error;
    if (!util::parse_xml(*text, reader, error))
        return false;
    if (!reader.name.empty())
        name_ = reader.name;
    for (const person &item : people_) {
        next_uid_ = std::max(next_uid_, uid_number(item.uid) + 1);
        for (const email_entry &email : item.emails)
            next_uid_ = std::max(next_uid_, uid_number(email.uid) + 1);
    }
    return true;
}

bool book::save(const fs::path &path) const {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    out += "<address-book";
    put(out, "name", name_);
    out += ">\n";
    for (const person &item : people_) {
        out += "  <person";
        put(out, "uid", item.uid);
        put(out, "first-name", item.first_name);
        put(out, "last-name", item.last_name);
        put(out, "nick-name", item.nick_name);
        put(out, "cn", item.common_name);
        out += ">\n    <address-list>\n";
        for (const email_entry &email : item.emails) {
            out += "      <address";
            put(out, "uid", email.uid);
            put(out, "alias", email.alias);
            put(out, "email", email.address);
            put(out, "remarks", email.remarks);
            out += " />\n";
        }
        out += "    </address-list>\n    <attribute-list>\n";
        for (const attribute &field : item.attributes) {
            out += "      <attribute";
            put(out, "uid", field.uid);
            put(out, "name", field.name);
            out += ">" + util::xml_escape(field.value) + "</attribute>\n";
        }
        out += "    </attribute-list>\n  </person>\n";
    }
    for (const group &item : groups_) {
        out += "  <group";
        put(out, "uid", item.uid);
        put(out, "name", item.name);
        put(out, "remarks", item.remarks);
        out += ">\n    <member-list>\n";
        for (const group::member &member : item.members) {
            out += "      <member";
            put(out, "pid", member.person_uid);
            put(out, "eid", member.email_uid);
            out += " />\n";
        }
        out += "    </member-list>\n  </group>\n";
    }
    out += "</address-book>\n";
    return util::write_file(path, out);
}

person &book::add(person value) {
    value.uid = std::to_string(next_uid_++);
    for (email_entry &email : value.emails)
        email.uid = std::to_string(next_uid_++);
    people_.push_back(std::move(value));
    return people_.back();
}

void book::remove_person(std::string_view uid) {
    std::erase_if(people_, [&](const person &p) { return p.uid == uid; });
    for (group &item : groups_)
        std::erase_if(item.members, [&](const group::member &member) {
            return member.person_uid == uid;
        });
}

const person *book::find_address(std::string_view address) const {
    for (const person &item : people_)
        for (const email_entry &email : item.emails)
            if (util::iequals(email.address, address))
                return &item;
    return nullptr;
}

std::vector<completion> book::group_addresses(const group &item) const {
    std::vector<completion> found;
    for (const group::member &member : item.members)
        for (const person &contact : people_) {
            if (contact.uid != member.person_uid)
                continue;
            for (const email_entry &email : contact.emails)
                if (email.uid == member.email_uid)
                    found.push_back({contact.display_name(), email.address});
        }
    return found;
}

void book_list::load(const fs::path &directory) {
    books_.clear();
    files_.clear();
    index_reader index;
    if (const auto text = util::read_file(directory / index_name)) {
        std::string error;
        util::parse_xml(*text, index, error);
    }
    for (const auto &[name, file] : index.entries) {
        book item(name);
        // A file name must stay inside the directory.
        if (file.find('/') != std::string::npos)
            continue;
        if (item.load(directory / file)) {
            books_.push_back(std::move(item));
            files_.push_back(file);
        }
    }
}

bool book_list::save(const fs::path &directory) const {
    std::string index = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                        "<addressbook>\n  <book_list>\n";
    bool ok = true;
    for (std::size_t i = 0; i < books_.size(); ++i) {
        char file[48];
        std::snprintf(file, sizeof file, "addrbook-%06zu.xml", i + 1);
        const std::string name = i < files_.size() ? files_[i] : file;
        ok = books_[i].save(directory / name) && ok;
        index += "    <book";
        put(index, "name", books_[i].name());
        put(index, "file", name);
        index += " />\n";
    }
    index += "  </book_list>\n</addressbook>\n";
    return util::write_file(directory / index_name, index) && ok;
}

book &book_list::primary() {
    if (books_.empty())
        books_.emplace_back("Addresses");
    return books_.front();
}

std::vector<completion> book_list::complete(std::string_view prefix,
                                            std::size_t limit) const {
    std::vector<completion> found;
    if (util::trim(prefix).empty())
        return found;
    for (const book &item : books_) {
        for (const person &contact : item.people()) {
            const std::string name = contact.display_name();
            const bool by_name = any_word_starts(name, prefix) ||
                                 util::istarts_with(contact.nick_name, prefix);
            for (const email_entry &email : contact.emails)
                if (by_name || util::istarts_with(email.address, prefix) ||
                    util::istarts_with(email.alias, prefix))
                    found.push_back({name, email.address});
        }
        for (const group &entry : item.groups()) {
            if (!util::istarts_with(entry.name, prefix))
                continue;
            std::vector<std::string> members;
            for (const completion &member : item.group_addresses(entry))
                members.push_back(member.formatted());
            if (!members.empty())
                found.push_back({"", util::join(members, ", ")});
        }
    }
    std::ranges::sort(found, {}, &completion::address);
    const auto duplicates =
        std::ranges::unique(found, {}, &completion::address);
    found.erase(duplicates.begin(), duplicates.end());
    if (found.size() > limit)
        found.resize(limit);
    return found;
}

bool book_list::contains(std::string_view address) const {
    return std::ranges::any_of(books_, [&](const book &item) {
        return item.find_address(address) != nullptr;
    });
}

} // namespace addr
