# Dialog, persistence and filesystem review — 2026-11-15

## Scope and Native reference

Reviewed **8 application-owned secondary window types**, updated all 8, and
reviewed the main window's search/filter/sort controls. Also reviewed the
**4 Native file-chooser instances** and **16 message-box call sites**; their
layouts remain owned by Native.

The dependency is pinned to `426a42f0f6077580dc2234d0adca8bafdba3e041`.
The reference was Native's `src/vision_edit_form.cpp`, plus `wnd_focus.cpp`
and `input_geometry.cpp`. The latter constrains single-line inputs to the
standalone form height, or the backend's ordinary text-edit height when no
standalone height is defined. Blaise uses those metrics after window creation,
with 12-pixel outer margins and 8-pixel row gaps. Old fixed field geometry was
removed from constructors. Shared form heights are calculated from their
contents; captions have bounded widths and align with their fields.

| Window | Entry points and changes |
| --- | --- |
| Input | Find in Message, New Folder, Rename Folder; caption/field/action rows sized together, initial field focus |
| Choice | Move/Copy to Folder; list and action row separated, initial list focus |
| Accounts | Tools → Accounts; measured label column, wider list buttons, separate checkbox rows, themed inputs and signature area |
| Preferences | Tools → Preferences; aligned checkboxes, numeric inputs, units and quote mark |
| Filter Rules | Tools → Filter Rules; full-width example and editor, aligned editing/action rows |
| Address Book | Tools and compose toolbar; resizing layout, aligned fields, controls recreated when reopening, uncommitted text cleared |
| Compose | New, Reply/Reply All, Forward, draft/queue editing; themed header rows, aligned labels, ordered fields and body |
| Network Log | Tools → Network Log; themed toolbar, resizing text area, initial text focus |

Shared forms assign explicit tab orders to content first, then OK and Cancel.
Compose, Address Book and the main search/panes assign their own sequences.
Painted labels have no tab stops; the off-screen attachment list has order zero
when empty. Native handles forward/backward traversal, wrapping and exclusion
of hidden/input-disabled controls. The Search Folder command now focuses its
field. Native file choosers cover opening/importing, exporting/saving messages
and attachments, attaching files and importing addresses.

## Settings and persistence

Reviewed Accounts, Preferences, Filter Rules, Address Book, compose drafts and
folder/window view settings. Corrections include:

- Start/restart the mail-check timer only after settings load or a successful
  save; apply the startup-mail-check option.
- Apply the threading preference to new folders; immediately save folder sort
  and threading changes. Save and restore window/pane dimensions, draining
  pending preference writes before capturing shutdown geometry.
- Commit pending filter-editor text on OK; report invalid replacements.
- Preserve copied settings on Cancel. Validate numeric bounds/ports, guard
  account-list selection callbacks and normalize invalid stored numbers.
- Remove obsolete IMAP stores after protocol changes. Server/user/port/directory
  changes use a distinct cache identity instead of reusing another mailbox's
  cached contents.
- Save before replacing engine settings in memory. Report failures with paths;
  restore account files when a companion password write fails.
- Report address-book read/import/save errors, roll back failed edits and
  ordinary multi-file write failures, and prevent editing an unreadable book.
- Stop refresh/shutdown from overwriting a folder list after a failed load.
  Inaccessible existing configuration is distinguished from missing files.
- Use exclusive temporary-file creation, checked writes/flush/close, and atomic
  replacement. Configuration/password files are created owner-only on Unix;
  missing application directories are created with mode 0700.

Fresh-process tests reload every Preferences control, account credentials,
signature and port, filters, contacts and folder view settings without relying
on a shutdown flush. Behavior checks cover quoted replies, wrapping, sent-copy
routing, new-folder threading and trash cleanup. Desktop tests cover saved
values, timer activation, Cancel/reopen and address-book close/reopen. Existing
mail tests exercise sending, receiving, filtering, drafts and queue processing.

## Filesystem and logging

Defaults follow the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/):
relative/empty XDG values are ignored. Explicit CLI paths override defaults.

| Content | Default location |
| --- | --- |
| Preferences, accounts, passwords, rules, folder configuration | `$XDG_CONFIG_HOME/blaise`, otherwise `~/.config/blaise` |
| New MH mail store, address books, POP3 receipt history | `$XDG_DATA_HOME/blaise`, otherwise `~/.local/share/blaise` |
| IMAP cache | `$XDG_CACHE_HOME/blaise/imapcache`, otherwise `~/.cache/blaise/imapcache` |
| Logs | Memory only, bounded to 2,000 entries; batch diagnostics use stderr |
| Atomic-write/migration scratch files | Exclusive sibling files/private staging directories, on the destination filesystem |

No system configuration, persistent file logger or runtime-file service was
introduced. The desktop client does not write to `/etc`, `/var/log` or
`/var/lib`. Existing `~/Mail`, folderlist mail paths and MH sidecar files remain
compatible with Claws. A missing home directory no longer falls back to the
installation/current directory.

A legacy `~/.blaise` is copied through a private staging directory only when the
XDG configuration destination does not exist. Source files remain intact;
an existing XDG profile wins. Address books and POP3 receipt history are copied
into the data directory. Disposable IMAP caches are rebuilt. Receipt/cache names
no longer embed unrestricted server/user path components. Explicit `--config`
profiles retain their self-contained legacy layout for compatibility and test
isolation. Configuration precedence remains built-in defaults, the chosen user
profile, then explicit runtime paths; there was no machine-wide configuration
layer to preserve.

The session logger needs no disk rotation or directory permissions. Existing
credential redaction remains; POP3 message bodies are now excluded from the
protocol log. Regression checks verify both exclusions. Claws import now
reports copy failures and creates private destination files.

## Validation and limits

Final results: **734/734 unit/restart checks**, **1,160/1,160 SDL desktop
checks**, and **1,170/1,170 GEM desktop checks** passed. The physical-input SDL
run passed **1,161/1,161** checks, including its extra focus assertion.

Validation commands and artifacts:

- Docker SDK: `make -j4 test ui-test headless` builds the desktop/headless
  applications and runs unit tests under ASan/UBSan plus desktop tests.
- `GEM_RUNTIME_ROOT= make platform-test PLATFORM=linux-gemix-gemd` builds and
  tests against the published GEM SDK/runtime, without the local runtime override.
- SDL/Xvfb screenshots of all eight secondary windows were inspected at
  1280×900: `build/ui-review/dialogs/`. Controls were aligned and visible,
  without observed clipping or overlap at that resolution.
- XTest physical keyboard injection moved between Preferences fields using Tab
  and Shift+Tab; the optional `BLAISE_TEST_KEYBOARD` desktop step verified the
  resulting text. `python3 scripts/test/dialogs.py` runs this check and captures
  the screenshots inside the SDL SDK. `BLAISE_TEST_STEP_MS` permits a slower
  inspection run.
- The existing `tests/ui/test_demo_mail.py --client bin/blaise-headless` fixture
  passed actual-client receive, authentication, SMTP capture, receipt history,
  restart and process-cleanup checks in Docker.
- The filesystem helper passed a Windows cross-compiler syntax check; this is
  not a Windows runtime or full application validation.

The new storage suite tests absent directories, absolute/relative/empty XDG
variables, legacy migration and precedence, owner permissions, permission-denied
writes, unreadable configuration, invalid numeric values, companion-file/index
rollback and protection of a failed folder-list load. Tests use isolated profiles.
Build/test logs are under `build/dialog-*.log` and
`build/platforms/linux-gemix-gemd/release/`.

Remaining limits:

- No manual high-DPI, very small-screen, or exhaustive backend matrix was run.
  Native-owned chooser/message-box visual and keyboard behavior was reviewed in
  integration, but not exercised exhaustively across operating systems.
- The existing serialized compatibility keys `message_font_size` and
  `show_html_as_text` remain inactive: Native's code editor uses its stock font,
  and Blaise has a text-only message viewer. Neither is exposed by Preferences.
  This review does not claim that every retained compatibility key affects
  rendering, nor add missing NNTP/local-account features.
- Individual files are replaced atomically. Account/password and address-book
  multi-file updates roll back ordinary failures but are not one crash-atomic
  transaction across files. Password storage remains unencrypted, owner-only.
- Save errors are reported; modal settings currently close after handing their
  copy to the asynchronous engine. A rejected save reopens with the previous
  persisted values, so users must re-enter the rejected edit after fixing access.
