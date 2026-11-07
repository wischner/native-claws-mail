#!/usr/bin/env python3
"""Run GDB/MI inside a backend's SDK and its matching desktop session."""
from pathlib import Path
import json
import os
import shlex
import sys

ROOT = Path(__file__).resolve().parents[2]
NATIVE = Path(os.environ.get('NATIVE_ROOT', ROOT.parent / 'native')).resolve()
PLATFORMS = json.loads((ROOT / 'scripts/build/platforms.json').read_text())
PREFIXES = {'cde-xephyr': 'CDE', 'openlook-xephyr': 'OPENLOOK',
            'window-maker-xephyr': 'WINDOW_MAKER'}

backend, *command = sys.argv[1:]
if backend not in PLATFORMS or not command:
    sys.exit('usage: linux.py BACKEND debugger [arguments]')
if len(command) == 1:
    command = shlex.split(command[0])  # cppdbg may supply "gdb --interpreter=mi"
if not os.environ.get('DISPLAY'):
    sys.exit('A running X11/Xwayland desktop with DISPLAY is required')

item = PLATFORMS[backend]
session = item.get('session')
args = ['docker', 'run', '--rm', '-i', '--name', f'blaise-debug-{os.getpid()}',
        '--cap-add=SYS_PTRACE',
        '--security-opt=seccomp=unconfined',
        '-v', f'{ROOT}:{ROOT}', '-v', f'{NATIVE}:{NATIVE}:ro', '-w', str(ROOT),
        '-e', 'ASAN_OPTIONS=detect_leaks=0',
        '-v', '/tmp/.X11-unix:/tmp/.X11-unix', '-e', 'DISPLAY']
authority = Path(os.environ.get('XAUTHORITY', str(Path.home() / '.Xauthority')))
if authority.is_file():
    args += ['-v', f'{authority}:/tmp/blaise.Xauthority:ro',
             '-e', 'XAUTHORITY=/tmp/blaise.Xauthority']
if session != 'cde-xephyr':
    args += ['--network=host', '-u', f'{os.getuid()}:{os.getgid()}']

wrapper = []
if session in PREFIXES:
    free = [number for number in range(100, 300)
            if not Path(f'/tmp/.X11-unix/X{number}').exists()
            and not Path(f'/tmp/.X{number}-lock').exists()]
    if len(free) < 2:
        sys.exit('No free nested display')
    prefix = PREFIXES[session]
    args += ['-e', f'{prefix}_DISPLAY=:{free[0]}',
             '-e', f'{prefix}_PARENT_DISPLAY=:{free[1]}']
    wrapper = [session]
elif session == 'x11':
    wrapper = ['bash', str(NATIVE / 'scripts/linux/x11/run-twm.sh')]
elif session == 'gemix':
    args += ['-e', f'NATIVE_ROOT={NATIVE}',
             '-e', f'BLAISE_GEMD={int(item.get("gemd", False))}']
    wrapper = ['bash', str(ROOT / 'scripts/debug/gem-session.sh')]
os.execvp('docker', args + [item['image']] + wrapper + command)
