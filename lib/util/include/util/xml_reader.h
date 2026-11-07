//
// A small streaming XML reader, replacing libgsf and libxml2
// wrapper. It covers what workbook files use: elements, attributes, character
// data, entities, comments, CDATA and processing instructions. DTDs
// are skipped, never interpreted.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UTIL_XML_READER_H
#define UTIL_XML_READER_H

#include <map>
#include <string>
#include <string_view>

namespace util {

// An element's attributes by name.
using xml_attributes = std::map<std::string, std::string, std::less<>>;

// Receives parse events in document order.
class xml_handler {
public:
    virtual ~xml_handler() = default;

    // An element opened.
    virtual void start_element(std::string_view name,
                               const xml_attributes &attributes) = 0;

    // An element closed.
    virtual void end_element(std::string_view name) = 0;

    // Character data arrived, entities resolved.
    virtual void characters(std::string_view text) = 0;
};

//
// Parse a document, delivering events to a handler.
//
// Parameters:
//      text        - Complete document.
//      handler     - Event receiver.
//      error       - Receives a description of the failure.
//
// Returns:
//      Whether the document was well formed. Events already delivered
//      stay delivered when parsing fails.
//
bool parse_xml(std::string_view text, xml_handler &handler,
               std::string &error);

// Escape text for XML character data or attribute values.
std::string xml_escape(std::string_view text);

} // namespace util

#endif
