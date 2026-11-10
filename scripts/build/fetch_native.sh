#!/bin/sh
# Fetch Native from GitHub into build/deps/native at NATIVE_REF, then print
# its path. NATIVE_REF is a commit, tag or branch; the default pins the commit
# Blaise was last built and tested against. NATIVE_URL overrides the
# repository. An existing clone is reused with the same URL, ref and patch;
# --update fetches the ref again (useful when NATIVE_REF names a branch).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
url=${NATIVE_URL:-https://github.com/wischner/native.git}
# Includes password_edit, tab_strip, shared toolbar metrics and GEM stock
# icons; see docs/notes/NATIVE-CHANGES.md.
ref=${NATIVE_REF:-c4191f853e2e23223d0fe50cf812309f08ce0a0d}
dest="$root/build/deps/native"
stamp="$root/build/deps/native.ref"
# Carry shared chrome fixes until they are in the pinned release.
patches="$root/scripts/build/patches"
want="$url $ref $(git hash-object "$patches"/*.patch)"
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
# A custom newer ref may already include this change.
for patch in "$patches"/*.patch; do
    if ! git -C "$dest" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$dest" apply "$patch"
    fi
done
echo "$want" > "$stamp"
echo "$dest"
