#!/bin/bash
# Check Blaise's file formats against a real Claws Mail, both ways. Claws
# scans a seeded folder under Xvfb, Blaise reads and changes what it wrote,
# and Claws' own --status-full must agree. Needs Docker and network access.
set -euo pipefail
writer=$1
work=$(mktemp -d)
chmod 777 "$work"
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/claws_session.sh
cleanup() {
    docker run --rm -v "$work:/work" ubuntu:24.04 rm -rf /work/home
    rmdir "$work"
}
trap cleanup EXIT
run_claws() {
    docker run --rm -v "$work:/work" -v "$script:/session.sh:ro" \
        ubuntu:24.04 bash /session.sh "$1"
}
run_claws seed
# BLAISE_SDK_IMAGE runs the writer in the image that built it.
if test -n "${BLAISE_SDK_IMAGE:-}"; then
    root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
    docker run --rm --init -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$root:$root" -v "$work:$work" -w "$root" "$BLAISE_SDK_IMAGE" \
        "$writer" "$work/home/Mail"
else
    "$writer" "$work/home/Mail"
fi
report=$(run_claws check)
echo "$report"
echo "$report" | grep -q '^ *1 *3 *4 *#mh/Mailbox/inbox$' || {
    echo 'FAILED: Claws does not see what Blaise wrote'
    exit 1
}
marks=$(echo "$report" | sed -n 's/^marks: //p')
for pair in '1 4' '2 2050' '3 642'; do
    echo "$marks" | grep -q "$pair" || {
        echo "FAILED: message and flags '$pair' did not survive Claws"
        exit 1
    }
done
echo 'claws checks passed'
