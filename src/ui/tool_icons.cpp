//
// Resolves shared Native command icons before embedded application artwork.
// Backend selection and toolbar state feedback remain Native's responsibility.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "tool_icons.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace native_embedded {
// Defined by Native rc in the generated resource translation unit.
std::span<const std::uint8_t> resource_data();
}

namespace ui {

void size_toolbar_separators(native::toolbar &bar) {
    auto items = bar.get_items();
    for (const auto &button : items) {
        if (!button.icon || !button.text.empty() ||
            button.kind == native::tool_kind::separator ||
            button.kind == native::tool_kind::control)
            continue;
        const int width = bar.get_item_bounds(button.id).w();
        for (auto &item : items)
            if (item.kind == native::tool_kind::separator)
                item.length = width;
        bar.set_items(std::move(items));
        return;
    }
}

const native::resources &graphics_resources() {
    static const auto pack =
        native::resources::decode(native_embedded::resource_data());
    return pack;
}

std::shared_ptr<const native::img> tool_icon(std::string_view command) {
    // Ask Native first: GEM supplies the same bitmaps as Ted. Other
    // backends, mail actions and commands without stock artwork keep
    // their existing resource variants.
    using icon_id = native::standard_icon_id;
    static constexpr std::array<std::pair<std::string_view, icon_id>, 12> standard{{
        {"fileNew", icon_id::file_new}, {"fileOpen", icon_id::file_open},
        {"fileSave", icon_id::file_save}, {"fileSaveAs", icon_id::file_save_as},
        {"fileClose", icon_id::file_close}, {"cut", icon_id::cut},
        {"copy", icon_id::copy}, {"paste", icon_id::paste},
        {"undo", icon_id::undo}, {"redo", icon_id::redo},
        {"print", icon_id::print}, {"find", icon_id::find},
    }};
    for (const auto &[name, id] : standard)
        if (name == command)
            if (auto icon = native::standard_icon(id))
                return icon;
    const auto &pack = graphics_resources();
    const native::resource_variant *entry = nullptr;
    try {
        entry = &pack.get("icons.toolbar." + std::string(command));
    } catch (const std::out_of_range &) {
        return nullptr;
    }
    if (entry->type != "icon")
        throw std::runtime_error("Toolbar resource is not an icon.");
    // img is immovable; direct initialization uses guaranteed copy elision.
    return std::shared_ptr<const native::img>(new native::img(
        native::img::decode(entry->data.data(), entry->data.size())));
}

} // namespace ui
