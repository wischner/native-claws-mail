#!/usr/bin/env python3
"""Put known mail and folders on the live test's Dovecot."""
import imaplib
import ssl

client = imaplib.IMAP4_SSL('127.0.0.1', 31993, timeout=10,
                           ssl_context=ssl._create_unverified_context())
client.login('tester', 'blaise-test')
SUBJECTS = ['Real_one', 'Caf=C3=A9_r=C3=A9sum=C3=A9']
for number, subject in enumerate(SUBJECTS, 1):
    message = (f'From: Ana <ana@example.org>\r\nTo: tester@example.org\r\n'
               f'Subject: =?UTF-8?Q?{subject}?=\r\n'
               f'Message-ID: <r{number}@example.org>\r\n'
               f'Date: Sat, 07 Nov 2026 10:00:0{number} +0000\r\n\r\n'
               f'Hello from a real server, {number}.\r\n')
    client.append('INBOX', None, None, message.encode())
for box in ('Sent', 'Trash', 'Work', 'Work/Sub'):
    client.create(box)
client.logout()
