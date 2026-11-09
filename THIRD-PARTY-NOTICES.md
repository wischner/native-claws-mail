# Third-party notices

## Claws Mail

`lib/` and `src/` are rebuilt from Claws Mail, revision `9226ea612`, Copyright
(C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto, GNU GPL version 3 or
later. Files derived from a specific Claws Mail file name it in their header and
keep its copyright line.

## Native

The Native UI library, MIT License, Copyright (C) 2026 Tomaz Stih, is built from
https://github.com/wischner/native and linked statically.

## Writer and Isaac

The build system, the UI structure, `lib/util/utf8` and `lib/util/xml_reader`
and the nine toolbar icons Blaise shares with them come from Writer
(`native-abiword`) and Isaac (`native-gnumeric`), Copyright (C) 2026 Tomaz Stih,
GPL-2.0-or-later, used here under version 3 as that licence permits.

## OpenSSL

When present at build time, Blaise links OpenSSL for TLS (the `make` build and
the Linux SDL2 release always have it), Apache License 2.0,
which is compatible with GPL version 3. See [TLS](docs/notes/TLS.md).
