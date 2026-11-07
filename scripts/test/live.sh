#!/bin/bash
# Run tests/live against real Dovecot and Mailpit in Docker, seeded with
# known mail, and check what Mailpit received. Removes both when done.
set -euo pipefail
binary=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
dovecot=blaise-live-dovecot
mailpit=blaise-live-mailpit
cleanup() { docker rm -f "$dovecot" "$mailpit" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup
docker run -d --name "$dovecot" -e USER_PASSWORD=blaise-test \
    -p 127.0.0.1:31143:31143 -p 127.0.0.1:31993:31993 \
    dovecot/dovecot:latest >/dev/null
docker run -d --name "$mailpit" -p 127.0.0.1:1025:1025 \
    -p 127.0.0.1:8025:8025 axllent/mailpit:latest >/dev/null
sleep 4
python3 -B "$root/scripts/test/live_seed.py"
"$binary"
python3 -B "$root/scripts/test/live_check.py"
