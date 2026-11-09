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
bypass it. Instead, every `make` build, test and run happens in the SDK image
(`scripts/build/docker/linux-sdl`, based on `wischner/gcc-x86_64-linux-sdl`),
which has no preload, so ASan and UBSan are always on. `make docker-test`
remains as an alias of `make test`.

Native's Debug build adds ASan on its own, so the Makefile passes
`NATIVE_ENABLE_DEVELOPER_CHECKS=OFF` whenever `SANITIZERS=` leaves ASan out.
Otherwise the final link fails with undefined `__asan_*` references in
`libnative.a`.

Debug objects record their sanitizer flags only in their contents. Run
`rm -rf build/sdl2/debug` after changing `SANITIZERS`, or linking fails with
undefined `__asan_*` symbols.
