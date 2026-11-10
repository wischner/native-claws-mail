#!/usr/bin/env python3
"""F5-only POP3/SMTP mailbox. Own both listeners for the debugger's lifetime."""
from contextlib import ExitStack
from email.message import EmailMessage
from email.policy import SMTP
from pathlib import Path
import argparse
import base64
import json
import os
import signal
import socketserver
import subprocess
import sys
import threading
import uuid

USER = 'demo'
PASSWORD = 'demo'
LIMIT = 2 * 1024 * 1024


def samples():
    subjects = ['Welcome to the Blaise test mailbox', 'Friday project notes',
                'Re: Friday project notes', 'Café, résumé and Unicode — Živjo!',
                'An HTML message', 'Your sample attachment', 'A longer message']
    bodies = [
        'This is a synthetic mailbox for F5 testing.\n\nTry reading, searching, '
        'replying, moving messages and opening attachments.\n'
        'Sending captures mail locally. Get Mail brings the captured message '
        'back into this inbox; nothing is delivered outside this test server.\n',
        'Hello Demo,\n\nThe project review is on Friday.\n'
        'Please bring your notes and the latest sketches.\n\nAna\n',
        'Thanks Ana, I will bring the sketches.\n\n'
        '> The project review is on Friday.\n> Please bring your notes.\n\nBen\n',
        'Hello, zdravo, bonjour!\n\nČebelica, naïve, schön, café.\n'
        'This message exercises UTF-8 headers and body text.\n',
        'The HTML version contains a heading, a list and a link.\n',
        'A small CSV file is attached. Try saving and opening it.\n',
        '\n'.join(f'Paragraph {n}: scroll through this synthetic message and '
                  'try selecting or searching its text.\n' for n in range(1, 36)),
    ]
    for number, (subject, body) in enumerate(zip(subjects, bodies), 1):
        message = EmailMessage(policy=SMTP)
        message['From'] = ['Ana <ana@example.test>', 'Ben <ben@example.test>'][number % 2]
        message['To'] = 'Demo User <demo@example.test>'
        message['Subject'] = subject
        message['Date'] = f'Tue, 10 Nov 2026 {9 + number:02}:15:00 +0000'
        message['Message-ID'] = f'<demo-{number}@example.test>'
        if number == 3:
            message['In-Reply-To'] = '<demo-2@example.test>'
            message['References'] = '<demo-2@example.test>'
        message.set_content(body)
        if number == 5:
            message.add_alternative('<html><body><h1>A sample HTML message</h1>'
                                    '<p>Try the rendered message and source view.</p>'
                                    '<ul><li>First item</li><li>Second item</li></ul>'
                                    '<a href="https://example.test/">Sample link</a>'
                                    '</body></html>', subtype='html')
        if number == 6:
            message.add_attachment(b'item,quantity\r\nPencils,3\r\nNotebooks,2\r\n',
                                   maintype='text', subtype='csv', filename='supplies.csv')
        yield f'demo-{number}', message.as_bytes()


class Mailbox:
    def __init__(self, root):
        self.root = root
        self.inbox = root / 'server/inbox'
        self.sent = root / 'server/sent'
        self.lock = threading.RLock()
        self.inbox.mkdir(parents=True, exist_ok=True)
        self.sent.mkdir(parents=True, exist_ok=True)
        marker = root / 'server/seeded'
        if not marker.exists():
            for uid, data in samples():
                (self.inbox / f'{uid}.eml').write_bytes(data)
            marker.touch()

    def snapshot(self):
        with self.lock:
            return [(p.stem, p.read_bytes()) for p in sorted(self.inbox.glob('*.eml'))]

    def delete(self, uids):
        with self.lock:
            for uid in uids:
                (self.inbox / f'{uid}.eml').unlink(missing_ok=True)

    def deliver(self, data):
        with self.lock:
            name = f'submitted-{uuid.uuid4().hex}.eml'
            (self.sent / name).write_bytes(data)
            (self.inbox / name).write_bytes(data)
        print(f'UAT mail captured: {name}', file=sys.stderr, flush=True)


class Handler(socketserver.StreamRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(60)

    def reply(self, text):
        self.wfile.write(text.encode('utf-8') + b'\r\n')

    def line(self):
        data = self.rfile.readline(65537)
        if len(data) > 65536:
            raise ValueError('line too long')
        return data

    def handle(self):
        try:
            self.session()
        except (OSError, ValueError):
            # Disconnects and malformed test commands affect this session only.
            return


class POP3(Handler):
    def multiline(self, data):
        for line in data.removesuffix(b'\r\n').split(b'\r\n'):
            self.wfile.write((b'.' if line.startswith(b'.') else b'') + line + b'\r\n')
        self.wfile.write(b'.\r\n')

    def session(self):
        self.reply('+OK Blaise F5 test mailbox')
        messages = []
        deleted = set()
        user = ''
        logged_in = False
        while raw := self.line():
            command, _, argument = raw.decode('utf-8').strip().partition(' ')
            command = command.upper()
            if command == 'QUIT':
                if logged_in:
                    self.server.mailbox.delete(messages[n][0] for n in deleted)
                self.reply('+OK Goodbye')
                return
            if command == 'CAPA':
                self.reply('+OK Capabilities')
                self.multiline(b'USER\r\nUIDL\r\nTOP\r\n')
            elif command == 'USER' and not logged_in:
                user = argument
                self.reply('+OK Password required')
            elif command == 'PASS' and not logged_in:
                logged_in = user == USER and argument == PASSWORD
                if logged_in:
                    messages = self.server.mailbox.snapshot()
                self.reply('+OK Mailbox ready' if logged_in else '-ERR Login failed')
            elif not logged_in:
                self.reply('-ERR Login first')
            elif command == 'STAT':
                active = [data for n, (_, data) in enumerate(messages) if n not in deleted]
                self.reply(f'+OK {len(active)} {sum(map(len, active))}')
            elif command in ('NOOP', 'RSET'):
                if command == 'RSET':
                    deleted.clear()
                self.reply('+OK')
            elif command in ('LIST', 'UIDL') and not argument:
                self.reply('+OK Messages')
                lines = [f'{n + 1} {uid if command == "UIDL" else len(data)}'
                         for n, (uid, data) in enumerate(messages) if n not in deleted]
                if lines:
                    self.multiline(('\r\n'.join(lines) + '\r\n').encode())
                else:
                    self.wfile.write(b'.\r\n')
            elif command in ('LIST', 'UIDL', 'RETR', 'TOP', 'DELE'):
                try:
                    fields = argument.split()
                    index = int(fields[0]) - 1
                    if index < 0 or index >= len(messages) or index in deleted:
                        raise ValueError()
                    uid, data = messages[index]
                    if command == 'TOP':
                        count = int(fields[1])
                        if count < 0:
                            raise ValueError()
                        head, _, body = data.partition(b'\r\n\r\n')
                        data = head + b'\r\n\r\n' + b'\r\n'.join(body.split(b'\r\n')[:count]) + b'\r\n'
                except (ValueError, IndexError):
                    self.reply('-ERR Invalid message')
                    continue
                if command == 'DELE':
                    deleted.add(index)
                    self.reply('+OK Marked for deletion')
                elif command in ('LIST', 'UIDL'):
                    self.reply(f'+OK {index + 1} {uid if command == "UIDL" else len(data)}')
                else:
                    self.reply(f'+OK {len(data)} octets')
                    self.multiline(data)
            else:
                self.reply('-ERR Unsupported command')


class SMTPServer(Handler):
    def session(self):
        self.reply('220 example.test Blaise F5 capture server')
        sender = False
        recipients = False
        while raw := self.line():
            command, _, argument = raw.decode('utf-8').strip().partition(' ')
            command = command.upper()
            if command in ('EHLO', 'HELO'):
                sender = recipients = False
                self.reply('250-example.test')
                self.reply(f'250 SIZE {LIMIT}')
            elif command == 'MAIL' and argument.upper().startswith('FROM:'):
                sender, recipients = True, False
                self.reply('250 Sender accepted')
            elif command == 'RCPT' and sender and argument.upper().startswith('TO:'):
                recipients = True
                self.reply('250 Recipient captured locally')
            elif command == 'DATA' and sender and recipients:
                self.reply('354 End with a single dot')
                data = bytearray()
                oversized = False
                while True:
                    line = self.line()
                    if not line:
                        return
                    if line in (b'.\r\n', b'.\n'):
                        break
                    if line.startswith(b'..'):
                        line = line[1:]
                    if len(data) + len(line) > LIMIT:
                        oversized = True
                    if not oversized:
                        data.extend(line)
                if oversized:
                    self.reply('552 Message too large')
                else:
                    self.server.mailbox.deliver(bytes(data))
                    self.reply('250 Message captured')
                sender = recipients = False
            elif command in ('NOOP', 'RSET'):
                if command == 'RSET':
                    sender = recipients = False
                self.reply('250 OK')
            elif command == 'QUIT':
                self.reply('221 Goodbye')
                return
            else:
                self.reply('503 Unsupported command or invalid command sequence')


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, handler, mailbox):
        super().__init__(('127.0.0.1', 0), handler)
        self.mailbox = mailbox


def configure(root, pop_port, smtp_port):
    config = root / 'config'
    config.mkdir(parents=True, exist_ok=True)
    (root / 'Mail').mkdir(exist_ok=True)
    (config / 'accountrc').write_text(f'''[Account: 1]
account_name=F5 synthetic mailbox
is_default=1
name=Demo User
address=demo@example.test
protocol=0
receive_server=127.0.0.1
smtp_server=127.0.0.1
user_id={USER}
set_popport=1
pop_port={pop_port}
set_smtpport=1
smtp_port={smtp_port}
ssl_pop=0
ssl_smtp=0
use_smtp_auth=0
use_apop_auth=0
remove_mail=0
receive_at_get_all=1
inbox=#mh/Mailbox/inbox
''')
    secret = base64.b64encode(PASSWORD.encode()).decode()
    passwords = config / 'passwordrc'
    passwords.write_text(f'[Passwords]\n1:recv={secret}\n')
    passwords.chmod(0o600)
    (root / 'session.json').write_text(json.dumps({'pop3': pop_port, 'smtp': smtp_port}) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a debugger command is required after --')
    mailbox = Mailbox(args.state)
    child = None
    def interrupt(signum, frame):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGINT, interrupt)
    with ExitStack() as stack:
        servers = [stack.enter_context(Server(POP3, mailbox)),
                   stack.enter_context(Server(SMTPServer, mailbox))]
        for server in servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()
            stack.callback(server.shutdown)
        configure(args.state, *(server.server_address[1] for server in servers))
        print('F5 synthetic mail: seven sample messages; outgoing mail stays local.',
              file=sys.stderr, flush=True)
        try:
            # Fetch through the actual client so its normal UID cache avoids
            # duplicates and the inbox is populated before the GUI opens.
            subprocess.run([str(args.client), '--config', str(args.state / 'config'),
                            '--mail', str(args.state / 'Mail'), '--receive'],
                           stdout=sys.stderr, check=True)
            child = subprocess.Popen(command, start_new_session=True)
            return child.wait()
        except KeyboardInterrupt:
            return 130
        finally:
            if child is not None and child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()


if __name__ == '__main__':
    sys.exit(main())
