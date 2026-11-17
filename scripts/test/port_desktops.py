#!/usr/bin/env python3
"""Exercise real mouse input in the CDE/OPEN LOOK SDK desktop sessions.

Run inside cde-xephyr or openlook-xephyr after building the debug client.
Requires their standard fonts, XTest, xwininfo and ImageMagick. Uses only
temporary synthetic mail; captures remain in build/ui-review/ports/.
"""
import argparse
import ctypes
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def wait_for(check):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError('Desktop stopped responding or did not open the message')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backend', choices=['linux-motif', 'linux-openlook'])
    parser.add_argument('--saved-window-size', nargs=2, type=int,
                        metavar=('WIDTH', 'HEIGHT'),
                        help='seed saved dimensions to check restoration on a smaller desktop')
    args = parser.parse_args()
    client = ROOT / 'build/platforms' / args.backend / 'debug/bin/blaise'
    out = ROOT / 'build/ui-review/ports'
    out.mkdir(parents=True, exist_ok=True)
    xlib = ctypes.CDLL('libX11.so.6')
    xtst = ctypes.CDLL('libXtst.so.6')
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    xlib.XOpenDisplay.restype = ctypes.c_void_p
    xlib.XFlush.argtypes = [ctypes.c_void_p]
    xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xlib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                  ctypes.c_uint, ctypes.c_uint]
    xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                        ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                        ctypes.c_int, ctypes.c_ulong]
    display = xlib.XOpenDisplay(os.environ.get('DISPLAY', '').encode())
    if not display:
        raise RuntimeError('Run this test inside the backend desktop session')

    def move(x, y):
        xtst.XTestFakeMotionEvent(display, -1, x, y, 0)
        xlib.XFlush(display)

    def button(down):
        xtst.XTestFakeButtonEvent(display, 1, int(down), 0)
        xlib.XFlush(display)

    def capture(name):
        subprocess.run(['import', '-window', 'root',
                        str(out / f'{args.backend}-{name}.png')], check=True)

    try:
        with tempfile.TemporaryDirectory(prefix='blaise-desktop-') as temporary, \
                (out / f'{args.backend}.log').open('w') as log:
            state = Path(temporary)
            if args.saved_window_size:
                (state / 'config').mkdir()
                width, height = args.saved_window_size
                (state / 'config/blaiserc').write_text(
                    f'[Common]\nwindow_width={width}\nwindow_height={height}\n')
            base = [str(client), '--config', str(state / 'config'),
                    '--mail', str(state / 'Mail')]
            process = subprocess.Popen(
                [sys.executable, '-B', str(ROOT / 'scripts/test/demo_mail.py'),
                 '--state', str(state), '--client', str(client), '--',
                 *base, '--select', '#mh/Mailbox/inbox'], stdout=log, stderr=log)
            try:
                def window_info():
                    assert process.poll() is None, 'Client exited during startup'
                    result = subprocess.run(['xwininfo', '-name', 'Inbox - Blaise'],
                                            capture_output=True, text=True)
                    return result.stdout if result.returncode == 0 else None

                info = wait_for(window_info)
                x = int(re.search(r'Absolute upper-left X:\s+(-?\d+)', info)[1])
                y = int(re.search(r'Absolute upper-left Y:\s+(-?\d+)', info)[1])
                window = int(re.search(r'Window id: (0x[0-9a-f]+)', info)[1], 16)

                def read_message(number, row_y):
                    def selected():
                        nonlocal x, y
                        assert process.poll() is None, 'Client exited during input'
                        # The WM can reparent/move the client after its title
                        # first appears. Resolve its current screen origin.
                        info = window_info()
                        if not info:
                            return False
                        x = int(re.search(r'Absolute upper-left X:\s+(-?\d+)', info)[1])
                        y = int(re.search(r'Absolute upper-left Y:\s+(-?\d+)', info)[1])
                        # Coordinates within the first two rows with the SDK's
                        # standard fonts. Retry while asynchronous mail loads.
                        move(x + 400, y + row_y)
                        button(True)
                        button(False)
                        time.sleep(.15)
                        summary = subprocess.check_output(
                            base + ['--summary', '#mh/Mailbox/inbox'], text=True)
                        rows = summary.splitlines()
                        return (len(rows) == 7 and
                                'N' not in rows[number - 1].split('\t')[1])
                    wait_for(selected)

                read_message(1, 117)
                info = window_info()
                desktop = subprocess.check_output(['xwininfo', '-root'], text=True)
                width = int(re.search(r'Width:\s+(\d+)', info)[1])
                height = int(re.search(r'Height:\s+(\d+)', info)[1])
                screen_width = int(re.search(r'Width:\s+(\d+)', desktop)[1])
                screen_height = int(re.search(r'Height:\s+(\d+)', desktop)[1])
                assert x >= 0 and y >= 0
                assert x + width + 8 <= screen_width, 'Window extends past right edge'
                assert y + height + 8 <= screen_height, 'Window extends past bottom edge'
                capture('selected')
                xlib.XResizeWindow(display, window, 740, 560)
                xlib.XFlush(display)
                time.sleep(.5)
                move(x + 162, y + 300)
                button(True)
                time.sleep(.1)
                move(x + 230, y + 300)
                time.sleep(.1)
                button(False)
                read_message(2, 137)
                capture('resized')
            except BaseException:
                capture('failed')
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
    finally:
        xlib.XCloseDisplay(display)
    print(f'{args.backend}: physical message selection and input after '
          'window resize/divider drag passed')


if __name__ == '__main__':
    main()
