//
// Resolves toolbar icons from the embedded Native resource pack. Backend
// selection belongs to native::resources; controls own theme feedback.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "tool_icons.h"

#include <stdexcept>
#include <string>

namespace native_embedded {
// Defined by Native rc in the generated resource translation unit.
std::span<const std::uint8_t> resource_data();
}

namespace ui {

const native::resources &graphics_resources() {
    static const auto pack =
        native::resources::decode(native_embedded::resource_data());
    return pack;
}

std::shared_ptr<const native::img> tool_icon(std::string_view command) {
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
