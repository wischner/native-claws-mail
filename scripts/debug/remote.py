#!/usr/bin/env python3
"""Deploy blaise to a VM and debug it there, as Native and charlie do."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SSH = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
HAIKU_DIR = '/boot/home/Projects/blaise'
HAIKU_HOST = os.environ.get('BLAISE_HAIKU_HOST', 'hotel')


def ssh(command):
    """Run one command on the VM, keeping GDB/MI stdout untouched."""
    subprocess.run(['ssh', *SSH, '-T', HAIKU_HOST, command], check=True)


def deploy_haiku(mode):
    """Copy the executables and F5 fixture; Haiku's SSH needs legacy SCP."""
    source = ROOT / 'build/platforms/haiku' / mode / 'bin'
    files = [source / 'blaise', source / 'test_desktop']
    if not all(path.is_file() for path in files):
        sys.exit('Build Haiku before deploying')
    files.append(ROOT / 'scripts/test/demo_mail.py')
    subprocess.run(['ssh', *SSH, '-T', HAIKU_HOST,
                    'mkdir -p ' + shlex.quote(HAIKU_DIR)],
                   check=True, stdout=sys.stderr)
    subprocess.run(['scp', *SSH, '-O', *map(str, files),
                    f'{HAIKU_HOST}:{HAIKU_DIR}/'],
                   check=True, stdout=sys.stderr)


def main():
    action, *arguments = sys.argv[1:]
    if action == 'deploy-haiku':
        deploy_haiku('debug')
    elif action == 'debug-haiku':
        demo_mail = arguments[:1] == ['--demo-mail']
        if demo_mail:
            arguments = arguments[1:]
        if len(arguments) == 1:
            arguments = shlex.split(arguments[0])
        if demo_mail:
            arguments = ['python3', '-B', HAIKU_DIR + '/demo_mail.py',
                         '--state', HAIKU_DIR + '/uat-mail',
                         '--client', HAIKU_DIR + '/blaise', '--'] + arguments
        ssh('cd ' + shlex.quote(HAIKU_DIR) + ' && exec ' +
            shlex.join(arguments))
    else:
        sys.exit('usage: remote.py deploy-haiku | debug-haiku [--demo-mail] debugger')


if __name__ == '__main__':
    main()
