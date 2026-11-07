//
// The MIME tree of a message. procmime.c's MimeInfo becomes a value
// tree of offsets into the raw message, so parsing allocates no body
// data and a part decodes only when something asks for it.
//
// Derived from Claws Mail src/procmime.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_PART_H
#define MIME_PART_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mime/header.h"

namespace mime {

// How a part's body is transfer-encoded.
enum class transfer_encoding {
    identity,
    quoted_printable,
    base64,
    uuencode
};

// One node of a message's MIME tree.
class part {
public:
    std::string type = "text";      // Lower case.
    std::string subtype = "plain";  // Lower case.
    std::string charset;
    std::string filename;           // Decoded; from either header.
    std::string description;        // Decoded.
    std::string content_id;
    std::string boundary;
    bool attachment = false;        // Disposition said so.
    transfer_encoding encoding = transfer_encoding::identity;
    header_block headers;
    std::size_t body_start = 0;     // In the raw message.
    std::size_t body_end = 0;
    std::vector<part> children;

    // Return "type/subtype".
    std::string mime_type() const;

    // Report whether this part is any multipart.
    bool is_multipart() const { return type == "multipart"; }

    // Return the body as stored, still transfer-encoded.
    std::string_view raw_body(std::string_view message) const;

    // Return the body with the transfer encoding removed.
    std::string decoded_body(std::string_view message) const;

    // Return a text part's content as UTF-8, with LF line ends.
    std::string text(std::string_view message) const;

    // Return the number of decoded bytes, estimated from the encoded size.
    std::size_t approximate_size() const;
};

//
// Parse a complete message.
//
// Notes:
//      Tolerates what real mail contains: a missing final boundary, a
//      boundary-less multipart, an unknown encoding. Anything that does
//      not parse becomes a single opaque part, never a failure. Nesting
//      stops at 32 levels, so a hostile message cannot exhaust the stack.
//      A message/rfc822 part is parsed into one child.
//
part parse_message(std::string_view raw);

// Call visit for each leaf of a tree, depth first.
template <typename Visitor>
void for_each_leaf(const part &root, Visitor &&visit) {
    if (root.children.empty()) {
        visit(root);
        return;
    }
    for (const part &child : root.children)
        for_each_leaf(child, visit);
}

// Return the part that best shows the message, or null.
const part *body_part(const part &root);

// Return the parts a user would call attachments.
std::vector<const part *> attachments(const part &root);

// Return the message's readable body as UTF-8 plain text.
std::string body_text(const part &root, std::string_view message);

} // namespace mime

#endif
