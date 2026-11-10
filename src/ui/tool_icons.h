//
// Standard Native command artwork with embedded application fallbacks.
// GEM and X11 receive binary ink masks; other backends receive color icons.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef UI_TOOL_ICONS_H
#define UI_TOOL_ICONS_H

#include <memory>
#include <string_view>

#include <native.h>

namespace ui {

// Return the immutable embedded graphics pack, owned for process lifetime.
const native::resources &graphics_resources();

// Return the icon of a toolbar command, or null when it has none.
std::shared_ptr<const native::img> tool_icon(std::string_view command);

// Give each group separator one icon button's measured width after layout.
void size_toolbar_separators(native::toolbar &bar);

} // namespace ui

#endif
