#!/bin/sh
# Configure and build Native as a consumer; called under the per-mode make lock.
set -eu
native_root=$1
native_build=$2
native_config=$3
flock "$BUILD/sdk.lock" sh "$ROOT/src/ui/bootstrap_sdk.sh"
cmake -S "$ROOT/src/ui" -DNATIVE_ROOT="$native_root" -B "$native_build" \
    -DTOOLKIT=SDL2 -DCMAKE_BUILD_TYPE="$native_config" -DBUILD_TESTING=OFF \
    -DNATIVE_ENABLE_DEVELOPER_CHECKS="${NATIVE_CHECKS:-ON}" \
    -DCMAKE_PREFIX_PATH="$BUILD/sdk/usr" \
    -DCMAKE_CXX_FLAGS="-I$BUILD/sdk/usr/include/x86_64-linux-gnu"
make --no-print-directory -C "$native_build" native native_rc
