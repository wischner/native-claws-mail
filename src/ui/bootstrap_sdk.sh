#!/bin/sh
# Obtain missing Ubuntu development headers locally; never install packages.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if test -e /usr/include/SDL2/SDL_ttf.h && test -e /usr/include/png.h; then
    exit 0
fi
sdk="$root/build/sdk"
if test -e "$sdk/complete"; then exit 0; fi
mkdir -p "$sdk/debs"
cd "$sdk/debs"
apt-get download libsdl2-dev libsdl2-ttf-dev libsdl2-ttf-2.0-0 \
    libx11-dev libxext-dev libsm-dev libice-dev libpng-dev libjpeg-dev \
    libjpeg-turbo8-dev zlib1g-dev libfontconfig-dev libfreetype-dev \
    x11proto-dev libxau-dev libxdmcp-dev libxcb1-dev
for package in ./*.deb; do dpkg-deb -x "$package" "$sdk"; done
python3 - "$sdk" <<'PY'
from pathlib import Path
import sys
for p in (Path(sys.argv[1])/'usr/lib/x86_64-linux-gnu').iterdir():
    if p.is_symlink() and not p.exists():
        runtime = Path('/usr/lib/x86_64-linux-gnu') / p.readlink().name
        if runtime.exists():
            p.unlink()
            p.symlink_to(runtime)
PY
touch "$sdk/complete"
