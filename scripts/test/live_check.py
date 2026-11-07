#!/usr/bin/env python3
"""Check what Mailpit received from the live test."""
import json
import sys
import urllib.request

API = 'http://127.0.0.1:8025/api/v1'


def fetch(path):
    """Return a decoded JSON reply."""
    with urllib.request.urlopen(API + path, timeout=10) as reply:
        return json.load(reply)


def require(condition, text):
    """Report one check and stop on failure."""
    print(('ok: ' if condition else 'FAILED: ') + text)
    if not condition:
        sys.exit(1)


messages = fetch('/messages')['messages']
require(len(messages) == 1, 'one message arrived')
message = fetch('/message/' + messages[0]['ID'])
require(message['Subject'] == 'Live café test', 'subject decoded')
require(message['From']['Address'] == 'tester@example.org', 'sender')
require(message['To'][0]['Name'] == 'Ana Č', 'recipient name decoded')
require('Sent over a real socket.' in message['Text'], 'body')
require('\n.leading dot' in message['Text'], 'dot stuffing undone by the server')
require('č' in message['Text'], 'body charset')
require(len(message['Attachments']) == 1, 'attachment')
require(message['Attachments'][0]['FileName'] == 'note.txt', 'attachment name')
# Mailpit writes its own Bcc header from the envelope, so its headers cannot
# show that Blaise left Bcc out; test_live checks the bytes filed in Sent.
envelope = {item['Address'] for item in message.get('Bcc') or []}
require(envelope == {'hidden@example.org'}, 'Bcc recipient in the envelope')
print('live delivery checks passed')
