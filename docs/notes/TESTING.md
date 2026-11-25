# Testing

UI tests use Native and nothing else, so one test source covers every backend.

| Layer | Runs | How |
| --- | --- | --- |
| `tests/unit/` | `make test` | Headless, with in-memory servers |
| `tests/ui/test_desktop.cpp` | `make ui-test`, `make platform-test` | The real `main_window` |
| `tests/ui/test_demo_mail.py` | Docker command below | F5 POP3/SMTP fixture, compiled client and GDB arguments |
| `tests/live/test_live.cpp` | `make live-test` | Real Dovecot and Mailpit in Docker |
| `scripts/test/claws.sh` | `make claws-test` | Real Claws Mail in Docker, both ways |

## Unit suites

| Suite | Covers |
| --- | --- |
| `test_mime` | Codecs, charsets, dates, addresses, headers, encoded words, MIME trees, HTML, rendering |
| `test_store` | Summaries, Claws' cache and mark files, MH, mbox, `folderlist.xml`, rc files, accounts |
| `test_net` | SMTP, POP3 and IMAP against fakes, the IMAP store, one round trip over a real local socket |
| `test_logic` | Conditions, rules, address books, composing, the queue format, the message list, reading layout |
| `test_engine` | Compose through queue to SMTP and sent, POP3 and IMAP collection with filters, failures, restart |

`tests/unit/fake_server.h` holds SMTP, POP3 and IMAP servers that implement
`net::transport`. They answer as a server does, so a protocol test is a conversation
with no sockets, and they make failures easy to stage: a dropped connection, a
refused recipient, a server that hides STARTTLS, text injected behind a STARTTLS
reply. They are written from the RFCs by the same hand as the clients, so they
share any misreading of them. That is what the live test is for.

## The live test

`make live-test` needs Docker and network access. `scripts/test/live.sh` starts Dovecot and Mailpit,
seeds a mailbox over IMAP, runs `test_live` and then checks what Mailpit received.

It covers implicit TLS and STARTTLS, certificate rejection of Dovecot's self-signed
certificate, a refused password, listing, reading, flags in both directions with a
second session, folders with a modified UTF-7 name, copying, moving, the trash,
sending with Cc, Bcc and an attachment through real SMTP, the copy filed in Sent,
and restart. POP3 is not covered, because the image serves no POP3.

## The Claws test

`make claws-test` needs Docker and network access, because it installs Claws Mail
from Ubuntu in a container. Claws scans a seeded folder under Xvfb and writes its
cache and marks. `write_claws_folder` opens that folder with Blaise's store, changes
flags and adds a message. Claws then runs again, and its own `--status-full` and the
mark file it rewrites must show exactly what Blaise did. The files from one such run
are kept in `tests/fixtures/claws/`, and `test_store` checks that Blaise reads them
and writes them back byte for byte.

## The desktop test

The test links the application's window and runs the engine on its real worker
thread with the fake servers behind `app::options::connect`. Results arrive
asynchronously, so the scenario is a list of steps that each wait for a condition,
ticked through `native::app::post`. `BLAISE_TEST_TRACE=1` prints the steps. It
reaches private members through `friend class ::desktop_test`, which keeps test
accessors out of the public interface.

It reads a folder, follows threads, searches, flags, replies and sends, saves and
reopens a draft, collects over POP3, deletes, moves, edits accounts with the
password field, preferences, filters and the address book, and shuts down. It checks
the suite's shell directly: strips, status part widths, window size, menu order.
It also checks theme-derived toolbar heights, joined embedded fields, pane
edges, button-width group separators in the main and compose windows, tree
keyboard expansion, and table selection, scrolling and column resizing.

**What it does not cover.** Native's `on_native_*` entry points sit just above each
backend, so the backend's own step from OS event to `on_native_*` is outside this
test. Most of the scenario calls `main_window`'s methods rather than synthesizing
input, and modal file choosers are not opened.

## F5 test mailbox

Select any debug profile in VS Code and press **F5**. The existing build task
runs first. The debug launcher then starts `scripts/test/demo_mail.py`, creates
an isolated POP3 account and fetches the samples through that port's compiled
client before opening the GUI. Linux profiles run the fixture inside their SDK
container and require Docker and an X11 or Xwayland desktop. SDL2 uses the
Make-built `bin/blaise-debug`; the other Linux profiles use their platform debug
builds. Haiku deploys the fixture alongside the client and runs it on the VM,
which needs Python 3.9 or newer available as `python3`.

CDE starts its desktop services as root, then runs the mailbox helper and GDB
as its `cde` user. `scripts/debug/cde-session.sh` repairs ownership of the Motif
demo state left by older launchers before that switch, preserving private file
modes and existing mail. Running the helper outside CDE leaves private account
files unreadable to the GUI, which prevents mailbox loading and account dialogs.

The seven initial messages include a reply thread, UTF-8 text, an HTML
alternative, a CSV attachment and a long body. HTML is shown through Blaise's
normal text conversion. Stock GEM fonts may lack some of the Unicode glyphs.
Try reading and marking messages, searching, expanding mailbox folders, saving the
attachment, moving mail, resizing panes and composing a reply. Sending captures
the message locally; **Get Mail** retrieves a copy in the test inbox. The fixture
does not deliver mail to external recipients.

Both listeners bind to `127.0.0.1` on automatically selected ports. The test
credentials are `demo` / `demo`; the generated account uses plain POP3 and SMTP
without SMTP authentication. This fixture exercises neither IMAP nor TLS; those
remain covered by the separate protocol and live-server tests.

Each port has its own persistent state. Linux uses ignored
`build/uat-mail/<backend>/` (for example, `build/uat-mail/linux-x11/`). GEM keeps
its existing `build/uat-mail/` state. Haiku uses
`/boot/home/Projects/blaise/uat-mail/` on the VM. Paths within each state directory:

| Path | Contents |
| --- | --- |
| `config/` | Test account, password and client settings |
| `Mail/` | Client MH folders, flags and downloaded messages |
| `server/inbox/` | POP3 samples and locally submitted messages |
| `server/sent/` | Captured SMTP messages as `.eml` files |
| `session.json` | Ports selected for the most recent session |

The launcher refreshes the test account's connection settings each time. Mail
and flags persist, and the normal UID cache prevents duplicate sample downloads.
Stopping debugging or exiting the debugger stops the fixture's listeners. To
reset a port's samples, stop debugging and delete its state directory. Deleting
all of `build/uat-mail/` resets every Linux port, including GEM. Normal
builds and packages do not initialize or include this test state or server.

Every F5 profile sets `startup-with-shell off` in GDB. Without this setting,
the `#` in `--select #mh/Mailbox/inbox` can become a shell comment; Blaise reports
`--select needs a value` and Rasta closes when the session exits. Preserve the
setting when editing profiles. Shell commands outside GDB still need quoted
mailbox identifiers, for example `--select '#mh/Mailbox/inbox'`.

To run the fixture regression test from the repository root:

```sh
python3 -B scripts/build/platforms.py linux-gemix-gemd --mode debug --build-only
docker run --rm -u "$(id -u):$(id -g)" \
  -e HOME=/tmp -e ASAN_OPTIONS=detect_leaks=0 \
  --cap-add=SYS_PTRACE --security-opt=seccomp=unconfined \
  -v "$PWD:/work" -w /work wischner/gcc-x86_64-gemix:latest \
  python3 -B tests/ui/test_demo_mail.py \
  --client build/platforms/linux-gemix-gemd/debug/bin/blaise
```

This uses temporary state under `build/`, leaving the interactive F5 mailbox
alone. For another Linux port, use its SDK image and executable and pass
`--profile 'X11, Xephyr'` (or the matching profile name) to the test. It checks
real-client receive and UID reuse, rejected POP3 credentials,
SMTP capture and dot handling, restart, normal exit and Stop cleanup. It also
runs GDB with the F5 setup commands to check literal mailbox arguments. It does
not automate the full VS Code UI; the GEM GUI launch and framebuffer were
checked separately through GDB/MI.

`python3 -B tests/ui/test_debug_launchers.py` checks all F5 profiles against
their launchers, including the executable and isolated mailbox paths, without
starting Docker or contacting the Haiku VM.

To check migration of an existing root-owned Motif demo mailbox, run this in the
Motif SDK after a debug build:

```sh
docker run --rm --init -e HOME=/tmp -e ASAN_OPTIONS=detect_leaks=0 \
  -v "$PWD:/work" -w /work wischner/gcc-x86_64-linux-motif:1.4.1 \
  python3 -B tests/ui/test_cde_session.py \
  --client build/platforms/linux-motif/debug/bin/blaise
```

This creates temporary root-owned state, reopens it through the CDE launcher,
and verifies that the desktop user can load all seven messages, private password
permissions remain intact, and no messages are lost or duplicated. The actual
Motif F5 command was also checked through GDB/MI, with XTest keyboard input
opening Accounts and Preferences from the Tools menu.

## Platforms

For CDE and OPEN LOOK, `scripts/test/port_desktops.py` additionally uses real
XTest mouse input. It opens a temporary seven-message mailbox, verifies that
clicking a message clears its unread flag, resizes the window, drags the divider,
and verifies another message can still be read. On OPEN LOOK it also types
`longer` into the toolbar search, selects the matching message, checks its
native vertical scrollbar and drags it to verify that the reader text moves.
It clears the field with Ctrl+A/Backspace and verifies that opening a short
message hides the scrollbar before proceeding. It saves screenshots and logs
under `build/ui-review/ports/`. Run it inside the matching SDK session after a
debug build; the SDK supplies its standard fonts, XTest, xwininfo and ImageMagick:

```sh
docker run --rm --init -e HOME=/tmp -e ASAN_OPTIONS=detect_leaks=0 \
  -e CDE_SCREEN=1280x900 -v "$PWD:/work" -w /work \
  wischner/gcc-x86_64-linux-motif:1.4.1 \
  timeout 40 cde-xephyr python3 -B scripts/test/port_desktops.py linux-motif
docker run --rm --init -u "$(id -u):$(id -g)" \
  -e HOME=/tmp -e ASAN_OPTIONS=detect_leaks=0 -e OPENLOOK_SCREEN=1280x900 \
  -v "$PWD:/work" -w /work native-openlook-sdk:2 \
  timeout 40 openlook-xephyr python3 -B scripts/test/port_desktops.py linux-openlook
```

CDE's session wrapper runs as root for its desktop services. These commands use
private headless parent displays and do not disturb an active F5 session.
The test also checks that the initial window fits the display. Pass
`--saved-window-size 1600 1000` after the backend name to exercise restoration of
an oversized saved window. Omit the `*_SCREEN` overrides to use each SDK's normal
Xephyr size. New windows default to 900 × 600; if the backend reports no monitor,
restoration also uses that size as its upper bound.

`make platform-test` runs `scripts/build/platforms.py`, which configures, builds
and runs the desktop test in each image from `scripts/build/platforms.json`. Logs
land in `build/platforms/<name>/<mode>/`. See [PLATFORM-STATUS.md](PLATFORM-STATUS.md).

## Sanitizers

`make test` builds and runs the unit suites under ASan and UBSan in the SDK image,
so a security agent preloaded on the host cannot stop them; see
[HOST-ASAN.md](HOST-ASAN.md). `make docker-test` is an alias.

## Where things run

Every `make` goal compiles in the SDK image. `make live-test` and
`make claws-test` start Dovecot, Mailpit and Claws Mail with Docker on the host,
then run the seed scripts and the test programs in the SDK image too, on the host
network, so the host needs no Python packages or OpenSSL runtime.

The dialog capture runner also supports OPEN LOOK: run
`python3 -B scripts/test/dialogs.py --backend linux-openlook --mode debug`
inside `native-openlook-sdk:2` with `openlook-xephyr`. It captures all eight
secondary window types and minimum-size Compose/Address Book layouts under
`build/ui-review/openlook-dialogs/`. The desktop suite checks captions against
controls, equal dialog button dimensions and content containment after resize.
