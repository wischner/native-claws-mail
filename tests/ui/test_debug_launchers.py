#!/usr/bin/env python3
"""Check F5 wiring without starting Docker, desktop sessions or the Haiku VM."""
from pathlib import Path
import json
import os
import runpy
import shlex
import sys
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
PROFILES = json.loads((ROOT / '.vscode/launch.json').read_text())['configurations']


def expand(value):
    return value.replace('${workspaceFolder}', str(ROOT))


class Launchers(unittest.TestCase):
    def linux_command(self, arguments):
        platforms = types.ModuleType('platforms')
        platforms.NATIVE = ROOT / 'build/deps/native'
        platforms.PLATFORMS = json.loads(
            (ROOT / 'scripts/build/platforms.json').read_text())
        platforms.gem_runtime_docker_args = lambda item: []
        with patch.dict(sys.modules, platforms=platforms), \
                patch.dict(os.environ, DISPLAY=':0'), \
                patch.object(sys, 'argv', ['linux.py', *arguments]), \
                patch.object(sys, 'path', list(sys.path)), \
                patch('os.execvp') as execute:
            runpy.run_path(str(ROOT / 'scripts/debug/linux.py'), run_name='__main__')
        return execute.call_args.args[1]

    def test_linux_profiles_seed_the_debugged_client_and_open_its_mailbox(self):
        states = set()
        for profile in PROFILES:
            pipe = profile['pipeTransport']['pipeArgs']
            if not pipe[0].endswith('/linux.py'):
                continue
            with self.subTest(profile=profile['name']):
                # cppdbg can pass the debugger and its options as one string.
                command = self.linux_command(pipe[1:] + ['/usr/bin/gdb --interpreter=mi'])
                self.assertIn(str(ROOT / 'scripts/test/demo_mail.py'), command)
                self.assertEqual(command[command.index('--client') + 1],
                                 expand(profile['program']))
                state = command[command.index('--state') + 1]
                self.assertNotIn(state, states)
                states.add(state)
                args = list(map(expand, profile['args']))
                self.assertEqual(args[args.index('--config') + 1], state + '/config')
                self.assertEqual(args[args.index('--mail') + 1], state + '/Mail')
                self.assertEqual(args[args.index('--select') + 1], '#mh/Mailbox/inbox')
                self.assertIn('set startup-with-shell off',
                              [s['text'] for s in profile['setupCommands']])
                self.assertEqual(command[-2:], ['/usr/bin/gdb', '--interpreter=mi'])

    def test_cde_switches_user_before_starting_the_mailbox(self):
        command = self.linux_command(['linux-motif', '--demo-mail', 'gdb'])
        session = command.index(str(ROOT / 'scripts/debug/cde-session.sh'))
        mailbox = command.index(str(ROOT / 'scripts/test/demo_mail.py'))
        self.assertLess(session, mailbox)
        self.assertEqual(command[session + 1],
                         command[command.index('--state') + 1])

    def test_normal_run_does_not_start_fixture(self):
        command = self.linux_command(['linux-sdl2', str(ROOT / 'bin/blaise')])
        self.assertNotIn('--state', command)
        self.assertEqual(command[-1], str(ROOT / 'bin/blaise'))

    def test_haiku_runs_fixture_and_debugger_on_vm(self):
        profile = next(p for p in PROFILES if p['name'] == 'Haiku, VM')
        arguments = profile['pipeTransport']['pipeArgs'][1:]
        with patch.object(sys, 'argv', ['remote.py', *arguments,
                                      '/boot/system/bin/gdb --interpreter=mi']), \
                patch('subprocess.run') as execute:
            runpy.run_path(str(ROOT / 'scripts/debug/remote.py'), run_name='__main__')
        command = shlex.split(execute.call_args.args[0][-1])
        self.assertIn('/boot/home/Projects/blaise/demo_mail.py', command)
        self.assertEqual(command[command.index('--client') + 1], profile['program'])
        state = command[command.index('--state') + 1]
        args = profile['args']
        self.assertEqual(args[args.index('--config') + 1], state + '/config')
        self.assertEqual(args[args.index('--mail') + 1], state + '/Mail')
        self.assertEqual(command[-3:], ['--', '/boot/system/bin/gdb', '--interpreter=mi'])


if __name__ == '__main__':
    unittest.main()
