# Platform status

Updated 2026-11-10. Results below are dated observations, not a claim that every
backend was retested on the final tree. The desktop test enters through Native's
public input hooks; it does not prove every backend's physical input routing.
See [TESTING.md](TESTING.md).

| Backend | Builds | Desktop test | Last recorded scope |
| --- | --- | --- | --- |
| SDL2 | Yes | 197/197 | 2026-11-10 pane/toolbar follow-up, before final unread-font and separator changes |
| X11 | Yes | 69/69 | 2026-11-07 baseline |
| Motif | Yes | 69/69 | 2026-11-07 baseline |
| OPEN LOOK | Yes | 69/69 | 2026-11-07 baseline |
| Window Maker | Yes | 69/69 | 2026-11-07 baseline |
| GEM (gemd) | Yes | **234/234**, debug with ASan/UBSan | 2026-11-10 final UI changes, pinned Native plus patches, local corrected GEM server |
| Wayland | Yes | **Not run** | 2026-11-07 build only |
| elementary | Yes | **Not run** | 2026-11-07 build only |
| Haiku | Cross-build | **Not run** | 2026-11-07 build only |
| Windows | Cross-build | **Not run** | 2026-11-07 build only |
| macOS | **Not built** | **Not run** | No recorded build |

SDL2 screens were compared with Isaac in the earlier audit. GEM now also has
Rasta framebuffer checks for toolbar heights, full button-width separator cells,
unread system-font text, pane edges, status dividers and the resize glyph. A real
Rasta mouse drag exercised window resizing. The GEM F5 launch was reproduced
through GDB/MI and kept running before an explicit debugger exit; POP3/SMTP
listeners stopped with the session. The separate fixture regression checks
receive, SMTP capture, UID reuse, authentication and lifecycle cleanup.

The final GEM run used `wischner/gcc-x86_64-gemix:latest` at digest
`sha256:891f7938bf7b468f8c2342fb2bb66d0dd1c2cce91ae10a6026b8c92628be6c09`
and a local protocol-3 GEM server with the compact-frame fixes. That image's
server did not yet include those fixes. See
[NATIVE-CHANGES.md](NATIVE-CHANGES.md#pane-edges-and-resize-corner) for the runtime
selection and validation details. The final UI changes have not been rerun across
all other backends, and not every physical keyboard/mouse path was exercised.

## Known limits

- **TLS exists only in the `make` build and on SDL2.** Both use
  `scripts/build/docker/linux-sdl`, which adds OpenSSL. The other SDK images
  have none, so those builds have no TLS and refuse accounts that ask for it.
  Secure mail on those backends needs OpenSSL added to their image the same way.
- **Windows:** the socket code compiles against Winsock but has never run.
- **macOS:** `gmtime_r`, POSIX sockets and `iconv` should all be there, but
  `lib/net/socket_transport.cpp` sends with `MSG_NOSIGNAL`, which macOS lacks
  (it uses `SO_NOSIGPIPE`). It has not been tried, so the release workflow has no
  macOS job yet.
