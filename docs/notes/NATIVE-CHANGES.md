# Native changes made for Blaise

These extend [Native](https://github.com/wischner/native);
`scripts/build/fetch_native.sh` pins the
commit Blaise builds against. A contract added to Native has to work on every
backend, so nothing here is SDL2-only.

| Change | Why | Backends |
| --- | --- | --- |
| `native::password_edit` | An account dialog must not show passwords, and `text_edit` has no masked mode | All, shared code |

`password_edit` follows `tab_strip`: one shared library-painted control. Masking
is not universal across the toolkits: Athena's `AsciiText` can only hide all echo,
Motif's `XmTextField` needs modify-verify callbacks and SDL2 has no field at all.
So it is a `canvas`, and typing reaches it through the path every backend already
routes for canvases. Its contract is in Native's `ARCHITECTURE.md` section 17, it
is taught in chapter 22 of Programming Native, its decision is recorded in
`docs/notes/CUSTOM.md`, and `check_password_edit` in
`native_ruler_toolbar_tests` covers it on every backend.

Isaac's `tab_strip` is also included in the pinned revision. Both controls are
committed and pushed, so the build fetches them from GitHub.

## Fixed along the way

- X11 `split_view`, `panel` and `tab_view` destroyed their widget before their
  children. Outside an Xt dispatch that frees the tree at once, so closing a window
  with nested split views crashed in `XtDestroyWidget`. Isaac found the same fault
  in `canvas`. Blaise's window is the first to nest split views on X11; `valgrind`
  found it. Motif and OPEN LOOK already had the right order.

- SDL2 `font_t::stock` latched on its first call. `code_edit` asks for metrics
  when constructed, before `app::run` initializes SDL2_ttf, so any window with a
  member `code_edit` kept the bitmap font for the whole process. Stock fonts now
  answer with the bitmap font until SDL2_ttf is ready, without latching. A screen
  comparison with Isaac found this; no test had. That also let the stock array
  outlive `TTF_Quit`, so releasing a font now checks `TTF_WasInit`.

## What did not need a change

### Unread text and toolbar group spacing

Unread subjects and senders use Native's standard table cell painter with
the same one-pixel overstrike used by `code_edit` for bold field labels.
They retain the system control font, text height, selection colors and
ellipsis behavior; no installed TrueType face is substituted.

Main and compose toolbar group separators use `tool_item::length`, measured
from an icon button after the theme's toolbar height has been applied. Each
separator occupies one button width without introducing fixed dimensions.
GEM draws its divider at the trailing edge of that slot, matching button
borders, so the visible blank cell is also a full button wide.
The sibling Native checkout already supports separator lengths; the pinned
dependency receives the same implementation through
`scripts/build/patches/native-toolbar-separator-length.patch`.

### Pane edges and resize corner

The mailbox tree keeps only its right border; the summary keeps left/bottom,
and the message pane keeps left/top. Window, toolbar and status edges provide
the remaining enclosure. The split views and both toolbars fill the entire
work width. Layout follows Native's bounds-change hook so programmatic and
interactive resizing both refit the panes. The status bar uses Native's standard
border settings and docks at the work-area bottom. GEM's theme draws a top
rule and full-height part dividers without a bottom rule; an additional
application border mask would erase the bottom pixel of those dividers.

Native's table header painter now honors omitted outer edges instead of
redrawing them as cell borders. Window-owned GEM status bars stop before the
resize corner; the private backend derives its span from AES `wind_calc`.
Nested document status bars and windows with native scroll tracks retain their
existing bounds. These shared fixes also live in `../native` and are carried
by `scripts/build/patches/native-pane-chrome.patch` for default dependency builds.

The published GEM image at digest
`sha256:14c2c1638d545a175a9e16489f32a47a392c2b1ce44e140f1fffba1a96b6b423`
reserved a four-pixel frame outside `WF_WORKXYWH`. Removing a Blaise border
mask restored the last separator pixel inside the 20-pixel status bar, but
could not close the gap between that work area and the actual window border.

The fix is in the GEM source at `/home/tomaz/repos/triglav-os/gem`: resizable
windows without scrollbars now have single-pixel right and bottom borders.
A standard resize glyph remains in the bottom-right corner, with top and left
rules; the outer frame supplies its other two edges. AES excludes the corner
from client visible rectangles and reserves its hit area, so repainting cannot
erase the glyph. Windows with scrollbars keep their track geometry, and
windows without SIZER do not gain a resize corner.

The corrected server is built on GEM revision `6e6c606` (protocol 3), using
the current latest Gemix image
`sha256:891f7938bf7b468f8c2342fb2bb66d0dd1c2cce91ae10a6026b8c92628be6c09`,
under GEM's `build/compact-frame/runtime`. This workspace selects it through the ignored
`build/gem-runtime.path`, consumed by both platform tests and the F5 launcher.
The published image does not yet include this frame change. `GEM_RUNTIME_ROOT` can
override the selection; see README for returning to the image's server.

Validation against the corrected server: 207/207 Blaise GEM desktop checks
passed. GEM's frame and host regression tests cover edge geometry, corner
hit testing and preserving the glyph when painting visible client rectangles.
A real Rasta mouse drag resized Blaise from 1002×783 to 882×703 outer pixels;
framebuffer checks before and after confirm the toolbar rules meet the right
frame and all three status separators meet the bottom frame. They also check
the resize corner has continuous top/left rules and no duplicate bottom/right
rules. All three focused GEM tests passed: `test_window_frame`,
`test_gemd_host`, and the upstream `test_menu_limits`. The final capture is
`build/ui-review/final-frame.png`; Rasta drag evidence is under
`build/ui-review/frame-drag-final/`.

### GEM toolbar consistency

The toolbar update uses Native revision
`c4191f853e2e23223d0fe50cf812309f08ce0a0d`, pinned in
`scripts/build/fetch_native.sh`. Normal Make, CMake and VS Code builds fetch
this revision automatically; no `NATIVE_ROOT` override is required. A custom
Native checkout must include the shared toolbar and standard-icon APIs.

The search/filter/sort fields use `border_sides::right` and
`tool_item::control_fill_width`, so adjacent controls meet at one separator
without their own top, bottom or left borders. Native still owns control
placement, vertical alignment, focus and dropdown behavior. The shared
`control_fill_width` option is carried as
`scripts/build/patches/native-toolbar-controls.patch` until included in the
next pinned Native revision; the patch is the same change as in `../native`.
The fetch stamp includes its hash, so ordinary builds update automatically.
Custom Native checkouts must also include this option.
The border follow-up passed 165/165 GEM debug checks and 158/158 SDL2 checks
using the default fetched dependency. A GEM framebuffer capture confirms
single vertical separators and no field-owned top or bottom rules.

The default dependency path was verified with plain `make -j8` and the GEM
debug platform build, both without `NATIVE_ROOT` or `NATIVE_REF` overrides.
The GEM debug build passed all 141 desktop checks in the latest Gemix image.

- `theme::get_toolbar_height()` exposes the backend's standard band. GEM calls
  `gemctl_chrome_layout`, the same helper Gemscape and Ted use. Both main rows
  and the composer use it after window creation instead of requesting 30 pixels.
- Native owns toolbar insets, square command slots and hosted-control alignment.
  GEM uses full-band slots and one-pixel control insets, preserving the bottom
  rule and full-size 16×16 icons. Shrinking the old layout alone would have
  reduced the image area below 16 pixels.
- Hosted fields respect the toolkit's input minimum. In the tested SDK, GEM
  uses two 20-pixel rows with 18-pixel fields; SDL2 uses a 24-pixel command row
  and a 28-pixel control row to contain its 24-pixel inputs and native insets.
- `standard_icon(standard_icon_id)` centralizes Ted's New, Open, Save, Save As,
  Undo, Cut, Copy and Paste masks. Unsupported commands return null; Blaise
  keeps its resource fallback. The exact artwork and GPL attribution live only
  in Native, not in each application.

Validation on 2026-11-10 used `wischner/gcc-x86_64-gemix:latest`, digest
`sha256:14c2c1638d545a175a9e16489f32a47a392c2b1ce44e140f1fffba1a96b6b423`,
with GEM revision `d940cf31ee9e85af0264a83abd1526d4475ffb0c` and the companion
Native working tree. GEM passed 141/141 desktop checks; SDL2 passed 134/134.
Checks cover both rows, resize/alignment, standard icon identity and dimensions,
tree keyboard expansion, table selection/scrolling/column resizing, and the
existing mail, compose, send, receive, search, settings and command scenarios.
GEM and Ted framebuffer captures confirmed the same 20-pixel bands; all 128
words of the eight shared bitmaps were compared with Ted's source. Gemscape's
layout was compared in source; its browser runtime was not exercised.

No custom tree/table widget needed replacement. `folder_tree` maps folder IDs
and counts into `native::tree_view`; `summary_table` uses Native's virtual
`table_model` and cell-content hook for bold unread messages. Message styling,
attachment/link handling and mail-specific icons remain application code.
Physical input through every desktop backend was not manually tested; the
automated scenarios use Native's public input entry points.

The controls below were already provided by Native and remain in use:

| Claws used | Blaise uses |
| --- | --- |
| `GtkCMCTree` folder view | `native::tree_view`, derived |
| `GtkCMCTree` message list | `native::table_view` over a `table_model`, derived for bold unread rows |
| `GtkTextView` message view | `native::code_edit`, read-only, with style runs |
| `GtkTextView` compose body | `native::code_edit`, which brings undo |
| Paned layouts | `native::split_view`, nested |
| Toolbars | `native::toolbar` with `tool_kind::control` search and combo boxes |
| Status bar | `native::status_bar` |
| Address completion | `native::combo_box`, `editable` style |
| File chooser | `native::open_file_dialog`, `save_file_dialog` |
| Dialogs | `native::modal_wnd`, `modeless_wnd`, `message_box`, `list`, `text_edit`, `check`, `button` |
| Clipboard | `native::clipboard` |
| Background threads | `native::app::post` |
| Pixmap themes | Native `rc` and `native::resources` |

## Requests for later

- **`native::wnd` has no way to hide a child.** Compose moves its attachment list
  out of the client area instead.
- **No context menus.** Claws relies on them for folder and message actions; here
  every action is in a top-level menu. `canvas`, `tree_view` and `table_view` have
  no context-menu signal, and there is no popup menu control.
- **`code_edit` has no word wrap.** The reading pane hard-wraps body text to 90
  columns before showing it. This is `compose::wrap_text`, so it is the same wrap a
  sent message gets.
- **`code_edit` has no selection-range setter.** Find in Message moves the caret to
  the match but cannot highlight it.
- **No way to open a URL.** A clicked link goes to the clipboard, with a status
  message. A portable `open_url` would replace that.
- **No static text control**, as Isaac also found. Dialogs paint their captions
  through `ui::form_window`.
- **`table_cell` has no font-weight field.** Unread cells use the native
  cell-content hook and overstrike the standard text, matching the existing
  `code_edit` bold treatment without substituting a different font.
- **No application timer.** Automatic mail checks use a `std::jthread` that posts
  through `app::post`.
