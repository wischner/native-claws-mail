# Dialog, persistence and filesystem review — 2026-11-15

## Scope and Native reference

Reviewed **8 application-owned secondary window types**, updated all 8, and
reviewed the main window's search/filter/sort controls. Also reviewed the
**4 Native file-chooser instances** and **16 message-box call sites**; their
layouts remain owned by Native.

This review used Native commit `426a42f0f6077580dc2234d0adca8bafdba3e041`.
See [Native integration](NATIVE-CHANGES.md) for the current dependency pin.
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

## OPEN LOOK dialog polish — 2026-11-24

Revisited all eight application-owned secondary windows with Native `f2211fa`
and `native-openlook-sdk:2`. The SDK change is also present in `../native`;
Blaise carries `openlook-button-caption.patch` and
`openlook-text-alignment.patch` until they are included upstream.

- Search now has a “Search:” caption without a tab stop with the toolbar background,
  an underlined edit field, and normal toolbar group spacing. OPEN LOOK uses
  whitespace without a vertical rule. Single-line XView text is vertically
  centered in its allocation to align with the prompt.
- Every application dialog action uses the shared 96-pixel button width and
  the current form-control height. Accounts' New, Delete and Default buttons
  use the same dimensions as OK/Cancel, Filters and Address Book actions.
- OPEN LOOK's native OLGX renderer centers ordinary push-button text using its
  own font measurements. Menu rows and buttons with menu marks retain their
  normal alignment; Native still owns the controls and input handling.
- Accounts reserves at least 80 pixels for its multiline signature editor,
  keeping its native frame clear of OK/Cancel. The list column is wide enough
  for two standard action buttons with an 8-pixel gap.
- Compose and Address Book enforce minimum dimensions and relayout on both
  portable bounds changes and native resize events. Compose's editor has
  matching side margins, and its bottom is calculated from the editor's actual
  top so it cannot extend into the attachment area or footer.
- Preferences, Find/New Folder/Rename, destination choices and Network Log
  retain their existing aligned layouts; form action buttons receive the same
  sizing and caption treatment. Native file choosers and notices retain their
  toolkit-managed layouts and benefit from the shared OLGX caption change.

Validation: **1,507/1,507** desktop checks on both SDL and OPEN LOOK, including
caption/control overlap, uniform action dimensions and minimum-size resizing.
Debug tests use ASan/UBSan with leak detection disabled as in the platform
runner. Physical OPEN LOOK input verified search typing, filtering, clearing,
message selection and input after resize/divider dragging. Screenshots of all
8 application window types, plus the small Compose and Address Book layouts,
are in `build/ui-review/openlook-dialogs/`. Native chooser/notice call sites
were reviewed; their complete platform-specific layouts were not exhaustively
exercised in this pass.

Reproduce the OPEN LOOK screenshots inside its isolated desktop:

```sh
docker run --rm --init -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -e ASAN_OPTIONS=detect_leaks=0 -e OPENLOOK_SCREEN=1280x900 \
  -v "$PWD:/work" -w /work native-openlook-sdk:2 \
  openlook-xephyr python3 -B scripts/test/dialogs.py \
  --backend linux-openlook --mode debug
```

The search follow-up was verified with physical typing/clearing and a fresh
OPEN LOOK capture: prompt, input text and All filter share the same baseline,
and the search/filter gap has no rule. The 1,507 desktop assertions pass on
OPEN LOOK and SDL; the alignment repair is carried in Native and the pinned
dependency.
