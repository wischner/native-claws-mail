//
// Message headers: an ordered list of fields, kept raw so a message can
// be rewritten without disturbing what it does not understand. Replaces
// the HeaderEntry tables of procheader.c.
//
// Derived from Claws Mail src/procheader.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_HEADER_H
#define MIME_HEADER_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mime {

// One header field; the value is unfolded, undecoded.
struct header_field {
    std::string name;
    std::string value;
};

// A message or part's header block.
class header_block {
public:
    //
    // Parse a block.
    //
    // Parameters:
    //      raw         - Text starting at the first header line.
    //      body_offset - Receives where the body begins.
    //
    // Notes:
    //      Parsing stops at the first empty line, or at a line that is
    //      neither a field nor a continuation, which then begins the body.
    //
    static header_block parse(std::string_view raw,
                              std::size_t *body_offset = nullptr);

    // Return all fields in order.
    const std::vector<header_field> &fields() const { return fields_; }

    // Return the first value of a field, ignoring case, or nothing.
    std::optional<std::string> find(std::string_view name) const;

    // Return the first value of a field, or an empty string.
    std::string get(std::string_view name) const;

    // Return every value of a field, in order.
    std::vector<std::string> get_all(std::string_view name) const;

    // Append a field.
    void add(std::string_view name, std::string_view value);

    // Replace the first field of a name, or append it.
    void set(std::string_view name, std::string_view value);

    // Remove every field of a name.
    void remove(std::string_view name);

private:
    std::vector<header_field> fields_;
};

// A Content-Type or Content-Disposition value taken apart.
struct parameterized_value {
    std::string value;
    std::map<std::string, std::string> parameters;
};

//
// Parse a structured value with parameters.
//
// Notes:
//      RFC 2231 continuations and charsets are reassembled, so
//      parameters arrive as plain UTF-8 with lower case names.
//
parameterized_value parse_parameters(std::string_view text);

// Return a header line folded at 78 columns, with LF line ends.
std::string fold_header(std::string_view name, std::string_view value);

} // namespace mime

#endif
