#!/bin/sh
# Fetch Native from GitHub into build/deps/native at NATIVE_REF, then print
# its path. NATIVE_REF is a commit, tag or branch; the default pins the commit
# Blaise was last built and tested against. NATIVE_URL overrides the
# repository. An existing clone is reused while it holds the same URL and ref;
# --update fetches the ref again (useful when NATIVE_REF names a branch).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
url=${NATIVE_URL:-https://github.com/wischner/native.git}
# Head of the fyodor-native-extensions branch, which carries password_edit
# and tab_strip; see docs/notes/NATIVE-CHANGES.md.
ref=${NATIVE_REF:-546c86283f4b0c9f357cf9f2ddf6941e5fada1ee}
dest="$root/build/deps/native"
stamp="$root/build/deps/native.ref"
want="$url $ref"
if test -e "$dest/include/native.h" && test "${1:-}" != --update &&
   test "$(cat "$stamp" 2>/dev/null)" = "$want"; then
    echo "$dest"
    exit 0
fi
if test ! -d "$dest/.git"; then
    rm -rf -- "$dest"
    mkdir -p "$dest"
    git init --quiet "$dest" >&2
fi
# A shallow fetch accepts a branch, a tag or a full commit hash alike.
git -C "$dest" fetch --quiet --depth 1 "$url" "$ref" >&2
git -C "$dest" checkout --quiet --force --detach FETCH_HEAD >&2
echo "$want" > "$stamp"
echo "$dest"
