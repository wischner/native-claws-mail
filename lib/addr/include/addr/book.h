//
// The address book. Claws' addrbook, addrcache, addritem and addrindex
// layers exist to fit several back ends behind one GTK tree; Blaise has
// one kind of book, so they reduce to these values. The file format is
// Claws' addrbook-NNNNNN.xml and addrbook--index.xml, so an existing
// address book moves across by copying the files.
//
// Derived from Claws Mail src/addrbook.c, src/addritem.c, src/addrindex.c
// and src/addr_compl.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef ADDR_BOOK_H
#define ADDR_BOOK_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace addr {

// One email address of a person.
struct email_entry {
    std::string uid;
    std::string alias;
    std::string address;
    std::string remarks;
};

// A named attribute of a person.
struct attribute {
    std::string uid;
    std::string name;
    std::string value;
};

// One contact.
struct person {
    std::string uid;
    std::string first_name;
    std::string last_name;
    std::string nick_name;
    std::string common_name;
    std::vector<email_entry> emails;
    std::vector<attribute> attributes;

    // Return the name to show.
    std::string display_name() const;
};

// A list of addresses, each named by person and email uid.
struct group {
    std::string uid;
    std::string name;
    std::string remarks;
    struct member {
        std::string person_uid;
        std::string email_uid;
    };
    std::vector<member> members;
};

// One completion candidate.
struct completion {
    std::string name;
    std::string address;

    // Return "Name <address>".
    std::string formatted() const;
};

// One book.
class book {
public:
    // Create an empty book.
    explicit book(std::string name = "Addresses") : name_(std::move(name)) {}

    // Read an addrbook-NNNNNN.xml file; returns whether it parsed.
    bool load(const std::filesystem::path &path);

    // Write an addrbook-NNNNNN.xml file.
    bool save(const std::filesystem::path &path) const;

    // Return the display name.
    const std::string &name() const { return name_; }

    // Rename the book.
    void set_name(std::string value) { name_ = std::move(value); }

    // Return the people.
    std::vector<person> &people() { return people_; }
    const std::vector<person> &people() const { return people_; }

    // Return the groups.
    std::vector<group> &groups() { return groups_; }
    const std::vector<group> &groups() const { return groups_; }

    // Add a person, assigning uids; returns the person.
    person &add(person value);

    // Remove a person by uid, also from every group.
    void remove_person(std::string_view uid);

    // Return a person holding an address, ignoring case, or null.
    const person *find_address(std::string_view address) const;

    // Return members of a group as addresses.
    std::vector<completion> group_addresses(const group &item) const;

private:
    std::string name_;
    std::vector<person> people_;
    std::vector<group> groups_;
    std::uint64_t next_uid_ = 1;
};

// Every book, with the index file that lists them.
class book_list {
public:
    // Load the index in a directory, then each book; a new one if none.
    void load(const std::filesystem::path &directory);

    // Save the index and all books.
    bool save(const std::filesystem::path &directory) const;

    // Return the books.
    std::vector<book> &books() { return books_; }
    const std::vector<book> &books() const { return books_; }

    // Return the first book, creating it if needed.
    book &primary();

    //
    // Find completions for what has been typed.
    //
    // Notes:
    //      Matches the start of any name word or address, ignoring case.
    //      Groups complete as well, to their member list.
    //
    std::vector<completion> complete(std::string_view prefix,
                                     std::size_t limit = 20) const;

    // Report whether an address is in any book.
    bool contains(std::string_view address) const;

private:
    std::vector<book> books_;
    std::vector<std::string> files_;
};

// Parse LDIF text into people (RFC 2849).
std::vector<person> import_ldif(std::string_view text);

// Parse vCard text into people.
std::vector<person> import_vcard(std::string_view text);

// Write people as LDIF.
std::string export_ldif(const std::vector<person> &people);

} // namespace addr

#endif
