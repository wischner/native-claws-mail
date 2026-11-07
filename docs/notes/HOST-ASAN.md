# ASan on hosts with a security agent

The coding standard asks for `-fsanitize=address,undefined` in debug builds. On
a machine where `/etc/ld.so.preload` loads a security agent, ASan binaries die
before `main`. Seen with `/usr/local/lib/AppProtection/libAppProtection.so`:

```text
#0  0x0000000000000000 in ?? ()
#1  ... in libAppProtection.so
#2  ... in dlsym () from libAppProtection.so
#3  ... in __sanitizer::InitTlsSize()
#4  ... in __asan::AsanInitInternal()
```

ASan's init calls `dlsym`, the agent intercepts it, and the agent's own lookup
is not ready yet. Without `-static-libasan` the same run stops earlier with
"ASan runtime does not come first".

The agent is the machine's security control, so the project does not disable or
bypass it. Instead:

- The Makefile drops ASan when `/etc/ld.so.preload` exists, so `make test` runs
  UBSan only. Override with `SANITIZERS=`.
- `make docker-test` runs the full ASan and UBSan suite in
  `wischner/gcc-x86_64-linux-sdl`, which has no preload.

Native's Debug build adds ASan on its own, so the Makefile passes
`NATIVE_ENABLE_DEVELOPER_CHECKS=OFF` whenever ASan is off. Otherwise the final
link fails with undefined `__asan_*` references in `libnative.a`.

Debug objects record their sanitizer flags only in their contents. Run
`rm -rf build/debug` after changing `SANITIZERS`, or linking fails with
undefined `__asan_*` symbols.
