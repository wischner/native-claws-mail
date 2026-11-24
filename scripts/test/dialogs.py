#!/usr/bin/env python3
"""Capture Blaise's dialogs and verify physical Tab/Shift+Tab on SDL/Xvfb.

Run in the existing SDL SDK after `make ui-build`; requires Xvfb, ImageMagick
and libXtst (already supplied by that SDK). For OPEN LOOK, run inside
openlook-xephyr with --backend linux-openlook --mode debug; keyboard search
is covered separately by port_desktops.py. Only synthetic test mail is used.
"""
import argparse
import ctypes
import os
import pathlib
import subprocess
import threading
import time

def keyboard_check():
    xlib = ctypes.CDLL('libX11.so.6')
    xtst = ctypes.CDLL('libXtst.so.6')
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    xlib.XOpenDisplay.restype = ctypes.c_void_p
    d = xlib.XOpenDisplay(b':93')
    xlib.XStringToKeysym.argtypes = [ctypes.c_char_p]
    xlib.XStringToKeysym.restype = ctypes.c_ulong
    xlib.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    xlib.XKeysymToKeycode.restype = ctypes.c_uint
    xlib.XFlush.argtypes = [ctypes.c_void_p]
    xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    def key(name, down=True):
        code = xlib.XKeysymToKeycode(d, xlib.XStringToKeysym(name.encode()))
        xtst.XTestFakeKeyEvent(d, code, int(down), 0)
        xlib.XFlush(d)
        time.sleep(.015)
    def tap(name):
        key(name); key(name, False)
    def replace(value):
        key('Control_L'); tap('a'); key('Control_L', False)
        for c in value: tap(c)
    # Click the minutes field, then reach wrap using only Tab.
    xtst.XTestFakeMotionEvent(d, -1, 590, 298, 0)
    xtst.XTestFakeButtonEvent(d, 1, 1, 0)
    xtst.XTestFakeButtonEvent(d, 1, 0, 0)
    xlib.XFlush(d)
    time.sleep(.05)
    for _ in range(3): tap('Tab')
    replace('77')
    key('Shift_L')
    for _ in range(3): tap('Tab')
    key('Shift_L', False)
    replace('19')
    xlib.XCloseDisplay(d)

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', choices=['sdl2', 'linux-openlook'], default='sdl2')
parser.add_argument('--mode', choices=['debug', 'release'], default='release')
args = parser.parse_args()
openlook = args.backend == 'linux-openlook'
out = root / 'build/ui-review' / ('openlook-dialogs' if openlook else 'dialogs')
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, BLAISE_TEST_TRACE='1', BLAISE_TEST_STEP_MS='700')
x = None
if not openlook:
    env.update(DISPLAY=':93', SDL_VIDEODRIVER='x11', SDL_RENDER_DRIVER='software', BLAISE_TEST_KEYBOARD='1')
    x = subprocess.Popen(['Xvfb', ':93', '-screen', '0', '1280x900x24', '-nolisten', 'tcp'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
client = (root / 'build/platforms/linux-openlook' / args.mode / 'bin/test_desktop'
          if openlook else root / 'build/sdl2' / args.mode / 'tests/test_desktop')
p = None
watchdog = None
try:
    time.sleep(0.5)
    p = subprocess.Popen([str(client)], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    watchdog = threading.Timer(120, p.kill)
    watchdog.daemon = True
    watchdog.start()
    selected = {'open reply','accounts','preferences','filters','cancel and reopen','choice dialog','address book','network log','small compose','small address book'}
    for line in p.stdout:
        print(line, end='', flush=True)
        step = line.removeprefix('step: ').strip()
        if step == 'preferences' and not openlook:
            time.sleep(.2)
            keyboard_check()
        if step in selected:
            time.sleep(0.2)
            subprocess.run(['import', '-window', 'root', str(out / (step.replace(' ', '-') + '.png'))], env=env, check=True)
    raise SystemExit(p.wait())
finally:
    if watchdog:
        watchdog.cancel()
    if p and p.poll() is None:
        p.kill()
        p.wait()
    if x:
        x.terminate()
        x.wait()
