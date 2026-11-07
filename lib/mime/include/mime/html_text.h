//
// Turns HTML mail into text. Blaise does not render HTML: as in Claws,
// a mail client shows words, not remote content, and that keeps tracking
// pixels and script out of the design rather than behind a preference.
//
// Derived from Claws Mail src/html.c and src/enriched.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MIME_HTML_TEXT_H
#define MIME_HTML_TEXT_H

#include <string>
#include <string_view>

namespace mime {

//
// Convert HTML to plain text.
//
// Parameters:
//      html        - UTF-8 HTML.
//
// Notes:
//      Blocks become lines, lists become bullets, links keep their
//      target in angle brackets, entities resolve, and script and style
//      content is dropped. No resource is ever fetched.
//
std::string html_to_text(std::string_view html);

// Convert text/enriched to plain text.
std::string enriched_to_text(std::string_view enriched);

} // namespace mime

#endif
