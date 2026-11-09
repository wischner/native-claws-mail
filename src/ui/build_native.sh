#!/bin/sh
# Configure and build Native as a consumer; called under the per-mode make
# lock, inside the SDL2 SDK image, which provides every development package.
set -eu
native_root=$1
native_build=$2
native_config=$3
cmake -S "$ROOT/src/ui" -DNATIVE_ROOT="$native_root" -B "$native_build" \
    -DTOOLKIT=SDL2 -DCMAKE_BUILD_TYPE="$native_config" -DBUILD_TESTING=OFF \
    -DNATIVE_ENABLE_DEVELOPER_CHECKS="${NATIVE_CHECKS:-ON}"
make --no-print-directory -C "$native_build" native native_rc
