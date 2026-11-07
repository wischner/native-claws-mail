# Blaise graphics resources

`blaise.json` declares 21 toolbar icons under `icons.toolbar.<command>`. Each has
an unqualified color PNG and a `color: monochrome` PNG. Native's
`resources::get()` selects the latter automatically on GEMix and X11.

Nine are the suite's, byte for byte the same as Writer's and Isaac's: `fileNew`,
`fileOpen`, `fileSave`, `undo`, `redo`, `cut`, `copy`, `paste` and `find`. Twelve
are Blaise's own: `getMail`, `sendMail`, `reply`, `replyAll`, `forward`,
`deleteMail`, `nextUnread`, `previousUnread`, `markMessage`, `addressBook`,
`attachFile` and `threadView`, drawn in the same hand with the same helpers and
palette. All are original project artwork, 16×16, not copies of Claws Mail's.
`export_icons.cpp` holds the drawing instructions.

Monochrome pixels are black with either zero or full alpha, so Native can tint
the mask white on a selected button.

## Module transfer

Claws' `src/pixmaps` XPM set and `stock_pixmap.c`'s theme loader are replaced by
this manifest and the public Native resource API. Icon themes are not ported.

| Entry point | Responsibility |
| --- | --- |
| `ui::graphics_resources()` | Decode the embedded bytes once into an immutable `native::resources` |
| `ui::tool_icon(command)` | Resolve `icons.toolbar.<command>` and decode the selected PNG; unknown commands return null |
| `export_icons` | Reproduce both asset sets from the drawing functions, outside normal builds |
| Native `rc` | Validate the manifest and generate the pack and embedded translation unit |

## Regenerate the artwork

From the repository root, after a normal Make build:

```sh
cmake --build build/native-release --target export_icons -j8
build/native-release/export_icons src/resources/icons
make resources
```
