# Blaise

Blaise is a mail client rebuilt from the ground up from
[Claws Mail](https://www.claws-mail.org), in modern C++20, on the Native UI library.
It is the sibling of [Writer](../native-abiword) and [Isaac](../native-gnumeric),
with the same menus, toolbars and status bar.

It reads and writes Claws' MH folders, caches, account, filter and address book
files, so an existing setup moves across. IMAP, POP3 and SMTP, threads, quick
search, filtering, attachments, an address book with completion, drafts and a
send queue work.

It is an early port. IMAP, TLS and SMTP are tested against real Dovecot and Mailpit
servers and the file formats against a real Claws Mail, but no large provider has
been tried. Passwords are in an owner-only file but are not encrypted. There
is no NNTP, PGP, spell checking, printing or plugins. See
[port status](docs/notes/PORT-STATUS.md) before trusting it with mail you care
about.

## Build and run

```sh
make -j8                     # bin/blaise: desktop
bin/blaise
bin/blaise --select '#mh/Mailbox/inbox:1'   # open at a folder or message
make headless -j8            # bin/blaise-headless: no display dependency
bin/blaise-headless --list
bin/blaise-headless --summary '#mh/Mailbox/inbox'
bin/blaise-headless --message '#mh/Mailbox/inbox:1'
bin/blaise-headless --receive
make debug -j8               # bin/blaise-debug with sanitizers
make clean                   # remove build/ and bin/
```

Dependencies: Linux, GCC with C++20, GNU make 4.3+, `flock`, CMake 3.20 and Python 3.
OpenSSL's headers enable TLS; `make TLS=none` builds without, and connections that
ask for TLS then fail with a message. See [TLS](docs/notes/TLS.md). Native comes
from the sibling `../native` checkout; override with `NATIVE_ROOT=/path/to/native`.
On Ubuntu without development headers the build fetches them into `build/sdk/` and
installs nothing.

Mail lives in `~/Mail` and settings in `~/.blaise`; `--mail` and `--config` move
them.

To move from Claws Mail, copy the configuration; the mail stays where it is:

```sh
bin/blaise --import-claws ~/.claws-mail
```

Passwords do not come across, because Claws encrypts them. Enter them under Tools,
Accounts. Keep a backup of `~/Mail` the first time.

**VS Code F5** builds and debugs the selected backend. Profiles are named
`<name>, <env>`: `SDL2, Local`, `X11, Xephyr`, `GEM, Rasta` and so on.

All application UI goes through Native's public API. The default build selects
Native's SDL2 backend; `make platform-test` builds the others. See
[platform status](docs/notes/PLATFORM-STATUS.md).

## Using it

| Action | Keys |
| --- | --- |
| New message | Ctrl+N |
| Get mail | Ctrl+I |
| Reply, reply to all | Ctrl+R, Ctrl+Shift+R |
| Forward | Ctrl+L |
| Delete | Ctrl+D |
| Move to a folder | Ctrl+M |
| Next, previous unread | Ctrl+., Ctrl+, |
| Find in the message | Ctrl+F, then F3 |

The box above the list searches sender and subject as you type. It also takes
Claws' quick search: `f alice & s report` for from and subject, `U` for unread,
`~T` for not marked, `b text` for the body.

Click an attachment line to save it. Click a link to copy it. Double-click a draft
or queued message to edit it. Filter rules are written in Claws' own syntax, under
Tools, Filter Rules:

```text
subject matchcase "[list]" move "#mh/Mailbox/lists" mark_as_read
```

## Tests

```sh
make test                    # unit tests, no display
make ui-test                 # the real window, through Native's input hooks
make docker-test             # unit tests under ASan and UBSan
make live-test               # real Dovecot and Mailpit, in Docker
make claws-test              # real Claws Mail, in Docker, both ways
make platform-test           # every backend SDK; PLATFORM=linux-x11 for one
make list-platforms
```

See [testing](docs/notes/TESTING.md). ASan cannot start on hosts that preload a
security agent; see [that note](docs/notes/HOST-ASAN.md).

## Documentation

- [Architecture](docs/ARCHITECTURE.md): layers and design decisions
- [Port status](docs/notes/PORT-STATUS.md): what has moved from Claws Mail, module
  by module
- [Native changes](docs/notes/NATIVE-CHANGES.md): what this project added to Native
- [TLS](docs/notes/TLS.md) and [licensing](docs/notes/LICENSING.md)
- [Coding standards](docs/standards/)

## License

GNU General Public License, version 3 or later, because Claws Mail is. See
[LICENSE](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.md).
