#!/usr/bin/env python3
"""Exercise the F5 mail fixture through real POP3/SMTP and the Blaise client."""
from pathlib import Path
import argparse
import json
import poplib
import signal
import smtplib
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def wait_for(check):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.05)
    raise AssertionError('Timed out waiting for demo mail session')


def closed(port):
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=.3):
            return False
    except OSError:
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True, type=Path)
    args = parser.parse_args()
    client = args.client.resolve()
    with tempfile.TemporaryDirectory(prefix='demo-mail-check-', dir=ROOT / 'build') as temporary:
        root = Path(temporary)
        base = [str(client), '--config', str(root / 'config'), '--mail', str(root / 'Mail')]
        wrapper = [sys.executable, '-B', str(ROOT / 'scripts/test/demo_mail.py'),
                   '--state', str(root), '--client', str(client), '--']
        with (root / 'session.log').open('w+') as log:
            process = subprocess.Popen(wrapper + [sys.executable, '-c',
                'import time; time.sleep(60)'], stdout=log, stderr=log)
            try:
                wait_for(lambda: len(list((root / 'Mail/inbox').glob('[0-9]*'))) == 7)
                ports = json.loads((root / 'session.json').read_text())
                pop = poplib.POP3('127.0.0.1', ports['pop3'], timeout=5)
                try:
                    pop.user('demo')
                    try:
                        pop.pass_('wrong')
                        raise AssertionError('wrong password accepted')
                    except poplib.error_proto:
                        pass
                    pop.pass_('demo')
                    assert pop.stat()[0] == 7
                    assert len(pop.uidl()[1]) == 7
                    pop.dele(1)
                    pop.rset()
                    assert pop.stat()[0] == 7
                    pop.quit()
                finally:
                    pop.close()
                summary = subprocess.check_output(base + ['--summary', '#mh/Mailbox/inbox'], text=True)
                assert 'Welcome to the Blaise test mailbox' in summary
                assert 'Your sample attachment' in summary
                # F5 passes the #mh identifier through GDB. Its default launch
                # shell treats an unquoted # as a comment and drops the value.
                profile = next(p for p in json.loads(
                    (ROOT / '.vscode/launch.json').read_text())['configurations']
                    if p['name'] == 'GEM, Rasta')
                debugger = ['gdb', '--batch', '--return-child-result']
                for setup in profile.get('setupCommands', []):
                    debugger += ['-ex', setup['text']]
                folder = profile['args'][profile['args'].index('--select') + 1]
                debugger += ['-ex', 'set args ' + ' '.join(base[1:] +
                             ['--select', folder, '--summary', folder]),
                             '-ex', 'run', str(client)]
                debug_summary = subprocess.check_output(debugger, text=True)
                assert 'Welcome to the Blaise test mailbox' in debug_summary
                subprocess.run(base + ['--receive'], check=True, stdout=log, stderr=log)
                assert len(list((root / 'Mail/inbox').glob('[0-9]*'))) == 7, 'duplicate download'
                raw = (b'From: Demo <demo@example.test>\r\nTo: Ana <ana@example.test>\r\n'
                       b'Subject: Captured reply\r\nMessage-ID: <roundtrip@example.test>\r\n'
                       b'\r\nHello locally.\r\n.A dot-prefixed line.\r\n')
                with smtplib.SMTP('127.0.0.1', ports['smtp'], timeout=5) as smtp:
                    smtp.sendmail('demo@example.test', ['ana@example.test'], raw)
                sent = list((root / 'server/sent').glob('*.eml'))
                assert len(sent) == 1 and sent[0].read_bytes() == raw
                subprocess.run(base + ['--receive'], check=True, stdout=log, stderr=log)
                assert len(list((root / 'Mail/inbox').glob('[0-9]*'))) == 8
                assert any(p.read_bytes() == raw for p in (root / 'Mail/inbox').glob('[0-9]*'))
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 130
                assert all(closed(port) for port in ports.values()), 'listeners leaked on Stop'
                # A normal debugger exit must also stop the fixture and keep
                # previously downloaded UIDs from creating duplicate messages.
                subprocess.run(wrapper + [sys.executable, '-c', 'pass'], check=True,
                               stdout=log, stderr=log, timeout=20)
                ports = json.loads((root / 'session.json').read_text())
                assert all(closed(port) for port in ports.values()), 'listeners leaked on exit'
                assert len(list((root / 'Mail/inbox').glob('[0-9]*'))) == 8
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    print('F5 demo mail: actual-client receive, UID reuse, authentication, SMTP '
          'capture, dot handling, restart and Stop/exit cleanup passed')


if __name__ == '__main__':
    main()
