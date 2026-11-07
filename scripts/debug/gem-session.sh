#!/usr/bin/env bash
# Private Rasta framebuffer; GEMD is used only by the client/server variant.
set -euo pipefail
session_dir=$(mktemp -d /tmp/blaise-rasta.XXXXXXXX)
export GEM_RESOURCE_DIR=/opt/gemix/share/gem
export GEM_VDI_WIDTH=1280 GEM_VDI_HEIGHT=900
export GEM_RASTA_FRAMEBUFFER="$session_dir/rasta.fb"
export GEM_RASTA_HOST=127.0.0.1
GEM_RASTA_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
export GEM_RASTA_PORT
export GEM_RASTA_CURSOR=off GEM_RASTA_INVERSE=off
viewer_pid=
cleanup() {
    if [[ -n "$viewer_pid" ]]; then kill "$viewer_pid" 2>/dev/null || true; wait "$viewer_pid" 2>/dev/null || true; fi
    cat "$session_dir/viewer.log" >&2
    rm -f "$session_dir/viewer.log" "$session_dir/rasta.fb"
    rmdir "$session_dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
/opt/gemix/bin/rasta --width 1280 --height 900 --bpp 32 --framebuffer "$GEM_RASTA_FRAMEBUFFER" \
    --port "$GEM_RASTA_PORT" --scale 1 --cursor off --inverse off >"$session_dir/viewer.log" 2>&1 &
viewer_pid=$!
for ((attempt=0; attempt<100; ++attempt)); do
    kill -0 "$viewer_pid"
    [[ -s "$GEM_RASTA_FRAMEBUFFER" ]] && break
    sleep .05
done
[[ -s "$GEM_RASTA_FRAMEBUFFER" ]]
if [[ "${BLAISE_GEMD:-0}" == 1 ]]; then
    bash "$NATIVE_ROOT/scripts/gemix/gemd-session.sh" "$@"
else
    "$@"
fi
