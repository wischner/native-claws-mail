# Platform status

Updated 2026-11-17. Results below are dated observations, not a claim that every
backend was retested on the final tree. The desktop test enters through Native's
public input hooks; it does not prove every backend's physical input routing.
See [TESTING.md](TESTING.md).

The 2026-11-11 run used the Native v1.0.5 pin with no carried patches, release
builds through `make platform-test PLATFORM=<backend>`, and
`wischner/gcc-x86_64-gemix:latest` at image 1.5.3 (GEM v1.0.4, digest
`sha256:db926faa198adf9d1e9809fdc74b9a1308b62fccd501f626591277a1fd646b7a`).
`make test` passed every suite.

| Backend | Builds | Desktop test | Last recorded scope |
| --- | --- | --- | --- |
| SDL2 | Yes | Passed | 2026-11-11 Native v1.0.5 |
| X11 | Yes | 221/224 | 2026-11-11 Native v1.0.5; same 3 failures on the previous pin |
| Motif | Yes | **1,160/1,160** | 2026-11-16 debug, edit-form pin plus split-layout patch; CDE physical-input check |
| OPEN LOOK | Yes | **1,160/1,160** | 2026-11-16 debug, edit-form pin plus pane-position/focus patch; physical-input check |
| Window Maker | Yes | 221/224 | 2026-11-11 Native v1.0.5; same assertion as X11 |
| GEM (gemd) | Yes | **234/234** | 2026-11-11 Native v1.0.5, image 1.5.3 compact frames |
| Wayland | Yes | Passed | 2026-11-11 Native v1.0.5 |
| elementary | Yes | Passed | 2026-11-11 Native v1.0.5 |
| Haiku | Cross-build | 1,151/1,160 | 2026-11-16 debug, edit-form pin plus collection-parent patch; startup fixed, 9 layout assertions remain |
| Windows | Cross-build | Passed | 2026-11-11 Native v1.0.5 |
| macOS | **Not built** | **Not run** | No recorded build |

The historical X11-family failures above are one check,
`window_.right_.get_bounds().x2() == client.w()` in
`tests/ui/test_desktop.cpp`: the right pane does not reach the client width.
X11 fails it identically with the previous Native pin and its patches, so it
predates v1.0.5. On GEM the window status bar now spans the client area up to
the one-pixel compact border, as no corner grip remains.

The 2026-11-16 Motif and OPEN LOOK fixes address actual startup, overlapping
panes and focus failures. The desktop geometry assertion now uses the pane's
allocated width, which also works with pane-local origins. Both ports passed
the full debug desktop scenario and real mouse selection before and after
resizing/dragging in their Xephyr sessions. OPEN LOOK still prints XView caret
warnings during some programmatic form-focus checks; these no longer terminate
the process. See [Native changes](NATIVE-CHANGES.md#fixed-along-the-way).

The 2026-11-17 Motif F5 check found a separate launcher failure: the mailbox
helper created private account files as root before CDE switched to its desktop
user. The launcher now repairs legacy demo-state ownership and runs both the
helper and debugger inside the CDE user session. The exact F5 command under
GDB/MI displayed the mailbox tree and messages; real keyboard menu input opened
Accounts and Preferences. A regression verifies migration of root-owned state,
private password permissions and preservation of all seven messages without
duplicate downloads.

The Haiku debug run on `hotel` completed the desktop scenario after fixing
collection attachment inside split panes. Its remaining assertions concern
right-pane width, toolbar height and compose separator width. A fresh F5 mailbox
also fetched seven messages and reached the GUI event loop under GDB. See
[Native changes](NATIVE-CHANGES.md#fixed-along-the-way) for the carried patch.

SDL2 screens were compared with Isaac in the earlier audit. GEM now also has
Rasta framebuffer checks for toolbar heights, full button-width separator cells,
unread system-font text, pane edges, status dividers and the resize glyph. A real
Rasta mouse drag exercised window resizing. The GEM F5 launch was reproduced
through GDB/MI and kept running before an explicit debugger exit; POP3/SMTP
listeners stopped with the session. The separate fixture regression checks
receive, SMTP capture, UID reuse, authentication and lifecycle cleanup.

The earlier 2026-11-10 GEM run used `wischner/gcc-x86_64-gemix:latest` at digest
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
