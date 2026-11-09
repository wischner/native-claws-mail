# TLS

TLS is the one thing below the UI that Blaise does not write. Everything else in
`lib/` uses the C++ standard library and POSIX. A TLS stack written for this
project would be a liability: mail credentials go through it.

Blaise links OpenSSL when it finds it at build time, as `make` (`TLS=openssl`)
and CMake's `find_package(OpenSSL)` do by default. `make TLS=none` builds without
it. The `make` image and the Linux SDL2 platform image both come from
`scripts/build/docker/linux-sdl`, which adds `libssl-dev`, so `make` and the
`linux-sdl2` release zip have TLS. The other platform SDK images have no OpenSSL;
see [PLATFORM-STATUS.md](PLATFORM-STATUS.md).

| Claws Mail | Blaise |
| --- | --- |
| GnuTLS, `common/ssl.c` | OpenSSL, `lib/net/socket_transport.cpp` |
| `common/ssl_certificate.c`: trust on first use, stored per server | The system's CA store, hostname checked |
| Certificate prompt in `gtk/sslcertwindow.c` | `accept_invalid_certificate` on the account |

The UI has no field for `accept_invalid_certificate` yet, so it can only be set in
`accountrc` as `blaise_accept_invalid_certificate=1`. That is deliberate for now:
a checkbox that disables verification should come with a way to see what is being
accepted. Self-signed servers need their certificate in the system store until
that exists.

## What the code guarantees

- TLS 1.2 is the minimum.
- The certificate chain and the host name are both verified, unless the account
  explicitly accepts invalid certificates. SNI is always sent.
- `tls_mode::starttls` is mandatory, not opportunistic. A server that does not
  offer it is an error, so a man in the middle cannot strip the upgrade. Each
  client has a test for this.
- `line_stream::start_tls` discards buffered input, because anything received
  before the handshake was unauthenticated and could have been injected. `test_net`
  covers it.
- Capabilities and the APOP timestamp learned before the upgrade are thrown away.
- A build without TLS fails any connection that asks for it, with a message. It
  never falls back to plain text.

`lib/net` sees only `net::transport`, so changing the library means changing one
file. The in-memory test servers implement the same interface, so the unit tests
need no certificate. `make live-test` covers real TLS against Dovecot, including
that verification rejects its self-signed certificate. The positive path through
a chain to a real CA, and hostname mismatch, have not been exercised.
