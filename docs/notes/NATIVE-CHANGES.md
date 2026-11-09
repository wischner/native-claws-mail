# Native changes made for Blaise

These extend [Native](https://github.com/wischner/native) on its
`fyodor-native-extensions` branch; `scripts/build/fetch_native.sh` pins the
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

Isaac's `tab_strip` is on the same branch. Both are committed and pushed, so the
build fetches them from GitHub.

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

Everything else is Native as it stood:

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
- **`table_view::draw_cell_content` sets the font itself**, so a subclass that wants
  a bold row must draw the text itself. A `font` field on `table_cell` would be
  simpler, and would also work on backends that do not use the portable painter.
- **No application timer.** Automatic mail checks use a `std::jthread` that posts
  through `app::post`.
