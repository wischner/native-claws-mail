#!/usr/bin/env bash
# Repair legacy root-owned demo state before CDE switches to its desktop user.
set -euo pipefail
state=$1
shift

# Match the SDK wrapper's account and home. It starts the desktop services as
# root, then runs the supplied command as this user.
if ! id cde >/dev/null 2>&1; then
    useradd --no-create-home --home-dir "${CDE_RUNTIME:-/tmp/cde-xephyr}/home" \
        --shell /bin/ksh cde
fi
mkdir -p "$state"
chown -R --no-dereference "$(id -u cde):$(id -g cde)" -- "$state"
exec cde-xephyr "$@"
