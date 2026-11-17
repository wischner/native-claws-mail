#!/usr/bin/env python3
"""Verify CDE can reopen a legacy root-owned F5 mailbox without losing mail.

Run as root in the Motif SDK; the session wrapper starts an isolated desktop
and switches to its cde account before running the mailbox and client.
"""
import argparse
import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True, type=Path)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('run this regression as root inside the Motif SDK')
    client = args.client.resolve()
    with tempfile.TemporaryDirectory(prefix='cde-mail-check-',
                                     dir=ROOT / 'build') as temporary:
        state = Path(temporary)
        fixture = [sys.executable, '-B', str(ROOT / 'scripts/test/demo_mail.py'),
                   '--state', str(state), '--client', str(client), '--']
        # Reproduce the original launch order: root seeds the client profile.
        subprocess.run(fixture + ['/bin/true'], check=True, timeout=20)
        password = state / 'config/passwordrc'
        assert password.stat().st_uid == 0
        assert password.stat().st_mode & 0o777 == 0o600
        messages = {p.name: p.read_bytes()
                    for p in (state / 'Mail/inbox').glob('[0-9]*')}
        assert len(messages) == 7

        base = [str(client), '--config', str(state / 'config'),
                '--mail', str(state / 'Mail')]
        summary = subprocess.check_output(
            ['bash', str(ROOT / 'scripts/debug/cde-session.sh'), str(state),
             *fixture, *base, '--summary', '#mh/Mailbox/inbox'],
            text=True, timeout=40)
        assert 'Welcome to the Blaise test mailbox' in summary
        assert 'Your sample attachment' in summary
        assert len(summary.splitlines()) == 7
        assert password.stat().st_uid == pwd.getpwnam('cde').pw_uid
        assert password.stat().st_mode & 0o777 == 0o600
        assert messages == {p.name: p.read_bytes()
                            for p in (state / 'Mail/inbox').glob('[0-9]*')}
    print('CDE F5: root-owned mailbox repaired; desktop user can load '
          'private account files; all seven messages preserved without duplicates')


if __name__ == '__main__':
    main()
