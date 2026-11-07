# Platform status

Recorded 2026-11-07, on the same tree. "Desktop test" is `tests/ui/test_desktop.cpp`
in the backend's SDK image, 69 checks, running under Xvfb or the SDK's display. It
enters above the backend, so it does not prove real input; see
[TESTING.md](TESTING.md).

| Backend | Builds | Desktop test |
| --- | --- | --- |
| SDL2 | Yes | 69/69, also under ASan and UBSan |
| X11 | Yes | 69/69 |
| Motif | Yes | 69/69 |
| OPEN LOOK | Yes | 69/69 |
| Window Maker | Yes | 69/69 |
| GEM (gemd) | Yes | 69/69 |
| Wayland | Yes | **Not run** |
| elementary | Yes | **Not run** |
| Haiku | Cross-build | **Not run** |
| Windows | Cross-build | **Not run** |
| macOS | **Not built** | **Not run** |

Screens were captured and inspected on SDL2 only, beside Isaac under the same
display. Nobody has typed into or clicked on Blaise by hand on any backend: input
in every run above came from the test.

## Known limits

- **TLS exists only in the local `make` build.** None of the SDK images above has
  OpenSSL, so every platform build in this table has no TLS and refuses accounts
  that ask for it. Secure mail on those backends needs OpenSSL added to the image.
- **Windows:** the socket code compiles against Winsock but has never run.
- **macOS:** `gmtime_r`, POSIX sockets and `iconv` should all be there, but it needs
  the remote Mac build and has not been tried.
