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
    xlib.XStringToKeysym.argtypes = [ctypes.c_char_p]
    xlib.XStringToKeysym.restype = ctypes.c_ulong
    xlib.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    xlib.XKeysymToKeycode.restype = ctypes.c_uint
    xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xlib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                  ctypes.c_uint, ctypes.c_uint]
    xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                        ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                        ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
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

    def key(name, down):
        symbol = xlib.XStringToKeysym(name.encode())
        code = xlib.XKeysymToKeycode(display, symbol)
        assert code, f'No keycode for {name}'
        xtst.XTestFakeKeyEvent(display, code, int(down), 0)
        xlib.XFlush(display)
        time.sleep(.05)

    def tap(name):
        key(name, True)
        key(name, False)

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

                # OPEN LOOK v0.5.0 fits the menu-button panel to 25 pixels,
                # removing 11 pixels of bottom padding from the old SDK.
                first_row_y = 106 if args.backend == 'linux-openlook' else 117
                if args.backend == 'linux-openlook':
                    # Real typing must filter the table, not just update the
                    # model through the public set_text() entry point.
                    move(x + 100, y + 61)
                    button(True)
                    button(False)
                    time.sleep(.15)  # Let the native panel finish taking focus.
                    for letter in 'longer':
                        tap(letter)
                    read_message(7, first_row_y)
                    capture('search')
                    # The reader must expose a real XView scrollbar, and
                    # dragging it must change the document, not just its thumb.
                    tree = subprocess.check_output(
                        ['xwininfo', '-id', hex(window), '-tree'], text=True)
                    bars = re.findall(
                        r'(0x[0-9a-f]+).*?  (\d+)x(\d+)\+\d+\+\d+'
                        r'  \+(-?\d+)\+(-?\d+)', tree)
                    bars = [item for item in bars
                            if int(item[1]) == 19 and int(item[2]) > 100]
                    assert bars, 'No native vertical scrollbar found'
                    reader_bar = max(bars, key=lambda item: int(item[4]))
                    bar_id, _, bar_height, bar_x, bar_y = reader_bar
                    bar_info = subprocess.check_output(
                        ['xwininfo', '-id', bar_id], text=True)
                    assert 'IsViewable' in bar_info, 'Reader scrollbar is hidden'
                    # The last vertical bar must belong to the lower reader.
                    assert int(bar_y) > y + 200, 'Reader scrollbar is missing'
                    parent_info = subprocess.check_output(
                        ['xwininfo', '-id', bar_id, '-tree'], text=True)
                    reader_id = re.search(
                        r'Parent window id: (0x[0-9a-f]+)', parent_info)[1]
                    reader_info = subprocess.check_output(
                        ['xwininfo', '-id', reader_id], text=True)
                    reader_width = int(re.search(r'Width:\s+(\d+)', reader_info)[1])

                    def reader_pixels():
                        return subprocess.check_output([
                            'import', '-window', reader_id, '-crop',
                            f'{reader_width - 20}x{bar_height}+0+0', 'RGB:-'])

                    before_scroll = reader_pixels()
                    move(int(bar_x) + 9, int(bar_y) + 30)
                    button(True)
                    time.sleep(.1)
                    move(int(bar_x) + 9, int(bar_y) + int(bar_height) - 30)
                    time.sleep(.1)
                    button(False)
                    time.sleep(.3)
                    assert reader_pixels() != before_scroll, 'Scrollbar did not scroll text'
                    capture('reader-scrolled')
                    move(x + 100, y + 61)
                    button(True)
                    button(False)
                    key('Control_L', True)
                    tap('a')
                    key('Control_L', False)
                    tap('BackSpace')
                read_message(1, first_row_y)
                if args.backend == 'linux-openlook':
                    time.sleep(.2)
                    bar_info = subprocess.check_output(
                        ['xwininfo', '-id', bar_id], text=True)
                    assert 'IsUnMapped' in bar_info, 'Short message retains scrollbar'
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
                read_message(2, first_row_y + 20)
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
    if args.backend == 'linux-openlook':
        print('OPEN LOOK: physical toolbar search and clearing passed')
        print('OPEN LOOK: long-message scrollbar drag and short-message hiding passed')
    print(f'{args.backend}: physical message selection and input after '
          'window resize/divider drag passed')


if __name__ == '__main__':
    main()
