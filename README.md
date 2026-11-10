# Blaise

Blaise is a mail client rebuilt from the ground up from
[Claws Mail](https://www.claws-mail.org), in modern C++20, on the Native UI library.
It is the sibling of [Writer](https://github.com/wischner/native-abiword) and
[Isaac](https://github.com/wischner/native-gnumeric),
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
git clone https://github.com/wischner/native-claws-mail.git
cd native-claws-mail
make                         # bin/blaise: desktop
make run                     # build, then open it on your desktop
make headless                # bin/blaise-headless: no display dependency
make debug                   # bin/blaise-debug with ASan and UBSan
make dist                    # dist/blaise-0.1.0-linux-sdl2.zip, as a release ships it
make clean                   # remove build/ and bin/
```

The host needs only Linux, Git, GNU make and Docker. `make run`, `make dist` and
the platform and live tests also use Python 3; `make dist` also needs `patchelf`
and `zip`. Every build goal runs inside one SDK image, so the host needs no
compiler, headers or `../native` checkout:

- **Image.** `scripts/build/docker/linux-sdl/Dockerfile` adds OpenSSL to
  `wischner/gcc-x86_64-linux-sdl:1.3.0` (GCC 11, CMake, SDL2). The first `make`
  builds it as `blaise-linux-sdl:<hash of the Dockerfile>`, so editing the
  Dockerfile builds a new image. `DOCKER_IMAGE=...` uses another image.
- **Native.** [Native](https://github.com/wischner/native) is fetched into
  `build/deps/native` at the commit pinned in `scripts/build/fetch_native.sh`.
  `NATIVE_REF=<commit, tag or branch> make` builds another revision;
  `NATIVE_ROOT=/path/to/native make` builds against your own checkout.
- **Output.** Objects go to `build/sdl2/<mode>/`, binaries to `bin/`, release
  zips to `dist/`.

TLS is on: the image has OpenSSL. `make TLS=none` builds without it, and
connections that ask for TLS then fail with a message. See [TLS](docs/notes/TLS.md).
`make run` and the `SDL2, Local` F5 profile run the binary in the same image,
drawing on your desktop through its X11 socket.

```sh
bin/blaise --select '#mh/Mailbox/inbox:1'   # open at a folder or message
bin/blaise-headless --list
bin/blaise-headless --summary '#mh/Mailbox/inbox'
bin/blaise-headless --message '#mh/Mailbox/inbox:1'
bin/blaise-headless --receive
```

The binaries link the image's Ubuntu 22.04 libraries. To run them outside Docker,
the host needs the same libraries (SDL2, SDL2_ttf, OpenSSL 3); the zip from
`make dist` lists them.

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
make test                    # unit tests under ASan and UBSan, no display
make ui-test                 # the real window, through Native's input hooks
make live-test               # real Dovecot and Mailpit, in Docker
make claws-test              # real Claws Mail, in Docker, both ways
make platform-test           # every backend SDK; PLATFORM=linux-x11 for one
make list-platforms
```

See [testing](docs/notes/TESTING.md). Tests run in Docker, so ASan works even
on hosts that preload a security agent; see [that note](docs/notes/HOST-ASAN.md).

## Desktop icon

Blaise's desktop icon is [`assets/icons/BLAISE.PNG`](assets/icons/BLAISE.PNG): 32x32
pixels, black ink on white paper, transparent outside the icon. The build
copies it next to the executable: `make` writes `bin/BLAISE.PNG`, and every
release zip has `BLAISE.PNG` beside `blaise`.

To build a GEMiX image, take `BLAISE.PNG` from the `linux-gemix-gemd` release
zip, convert it with GEM's `resgen` and install it under the lowercase program
name, matching the program entry `/opt/gem/Programs/blaise`:

```sh
resgen icons -o blaise.rsc BLAISE.PNG
install -m 0644 blaise.rsc /opt/gem/share/gem/icons/blaise.rsc
```

GEM Desktop then shows the icon for `blaise` in the Programs folder. Without
the file, Desktop shows the standard program icon. GEM's
[program deployment guide](https://github.com/triglav-os/gem/blob/main/docs/guides/PROGRAM_DEPLOYMENT.md)
explains how Desktop finds icons.

## Releases

Pushing a `vX.Y.Z` tag runs `make` and `make test`, builds every backend and
publishes one zip per platform on the GitHub release page:

```sh
git tag v1.0.0
git push origin v1.0.0
```

See [releasing](docs/notes/RELEASING.md).

## Documentation

- [Architecture](docs/ARCHITECTURE.md): layers and design decisions
- [Port status](docs/notes/PORT-STATUS.md): what has moved from Claws Mail, module
  by module
- [Native changes](docs/notes/NATIVE-CHANGES.md): what this project added to Native
- [Releasing](docs/notes/RELEASING.md): the tag-triggered build and its zips
- [TLS](docs/notes/TLS.md) and [licensing](docs/notes/LICENSING.md)
- [Coding standards](docs/standards/)

## License

GNU General Public License, version 3 or later, because Claws Mail is. See
[LICENSE](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.md).
