"""Drives the player through a QML test snippet and takes screen shots (no hardware, no network).

usage: python ui-check.py <staged-app-dir> <out-dir> <snippet.qml> [KEY=VALUE ...] [app arguments ...]

KEY=VALUE replaces @KEY@ in the snippet (file URLs and the like).

The snippet is appended inside the staged Main.qml (restored afterwards) and talks to this script
through log lines:
  UITEST_SHOT <name>                 screen shot of the main window plus the 60px below it (the pill)
  UITEST_CLICK <x> <y>               a left click at screen coordinates
  UITEST_DRAG <x0> <y0> <x1> <y1>    a left-button drag at screen coordinates
  UITEST_KEY <vk> [count]            key presses (Windows virtual-key code) to the focused window
  UITEST_DONE / UITEST_FAIL <why>    the verdict
The log is flushed by the next log call, so the snippet keeps a heartbeat going.
Clicks and drags move the real pointer for about a second.
"""
import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path

app, out, snippet_path = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
defines = dict(a.split('=', 1) for a in sys.argv[4:] if '=' in a and not a.startswith('-'))
extra = [a for a in sys.argv[4:] if not ('=' in a and not a.startswith('-'))]
out.mkdir(parents=True, exist_ok=True)
work = out / 'work'
work.mkdir(exist_ok=True)

main = app / 'qml/Veyra/Main.qml'
original = main.read_bytes()
source = original.decode('utf-8-sig')
index = source.rfind('}')
snippet = snippet_path.read_text(encoding='utf8')
for key, value in defines.items():
    snippet = snippet.replace('@' + key + '@', value)
main.write_text(source[:index] + snippet + source[index:], encoding='utf8')

user32 = ctypes.windll.user32
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004


class RECT(ctypes.Structure):
    _fields_ = [('l', ctypes.c_long), ('t', ctypes.c_long), ('r', ctypes.c_long), ('b', ctypes.c_long)]


def main_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and not user32.IsIconic(hwnd):
            rect = RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            found.append((hwnd, rect))
        return True
    user32.EnumWindows(visit, 0)
    found.sort(key=lambda w: (w[1].r - w[1].l) * (w[1].b - w[1].t), reverse=True)
    return found[0] if found else None


def windows_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(visit, 0)
    return found


def raise_app(pid, on):
    # Topmost for the shot only, so whatever else is on screen (a terminal, the IDE) is not in it.
    HWND_TOPMOST, HWND_NOTOPMOST, flags = -1, -2, 0x0001 | 0x0002 | 0x0010   # NOSIZE | NOMOVE | NOACTIVATE
    # Largest first: each call goes to the top, so the pill and popups end above the main window.
    def area(hwnd):
        rect = RECT()
        user32.GetWindowRect(ctypes.c_void_p(hwnd), ctypes.byref(rect))
        return (rect.r - rect.l) * (rect.b - rect.t)
    for hwnd in sorted(windows_of(pid), key=area, reverse=True):
        user32.SetWindowPos(ctypes.c_void_p(hwnd), ctypes.c_void_p(HWND_TOPMOST if on else HWND_NOTOPMOST), 0, 0, 0, 0, flags)


def shot(pid, path):
    win = main_window(pid)
    if not win:
        return False
    _, r = win
    raise_app(pid, True)
    time.sleep(0.3)
    # Screen pixels: the picture is a native D3D window and the pill is a window of its own.
    try:
        from PIL import ImageGrab
        ImageGrab.grab(bbox=(r.l, r.t, r.r, r.b + 60), all_screens=True).save(str(path))
    finally:
        raise_app(pid, False)
    return True


def move(x, y):
    user32.SetCursorPos(int(x), int(y))


def click(x, y):
    move(x, y)
    time.sleep(0.15)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def drag(x0, y0, x1, y1):
    move(x0, y0)
    time.sleep(0.15)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    for i in range(1, 31):
        time.sleep(0.02)
        move(x0 + (x1 - x0) * i / 30, y0 + (y1 - y0) * i / 30)
    time.sleep(0.15)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


result = 1
proc = None
try:
    log = out / 'app.log'
    if log.exists():
        log.unlink()
    env = os.environ.copy()
    env.update(VEYRA_LOG_FILE=str(log), QML_DISABLE_DISK_CACHE='1', QML2_IMPORT_PATH=str(app / 'qml'),
               TEMP=str(work), TMP=str(work))
    command = [str(app / 'veyra_qml_ui.exe'), '--data-dir', str(work / 'profile'), '--exit-after', '280000'] + extra
    proc = subprocess.Popen(command, cwd=app, env=env, stdout=(out / 'stdio.log').open('w'), stderr=subprocess.STDOUT)
    seen = 0
    verdict = None
    deadline = time.time() + 290
    while time.time() < deadline and verdict is None:
        time.sleep(0.1)
        lines = log.read_text(encoding='utf8', errors='replace').splitlines() if log.exists() else []
        for line in lines[seen:]:
            if 'UITEST_SHOT ' in line:
                name = line.split('UITEST_SHOT ')[1].strip()
                time.sleep(0.2)
                print('shot', name, 'ok' if shot(proc.pid, out / f'{name}.png') else 'NO WINDOW')
            elif 'UITEST_CLICK ' in line:
                x, y = (float(v) for v in line.split('UITEST_CLICK ')[1].split()[:2])
                raise_app(proc.pid, True)
                time.sleep(0.3)
                try:
                    click(x, y)
                finally:
                    raise_app(proc.pid, False)
            elif 'UITEST_DRAG ' in line:
                x0, y0, x1, y1 = (float(v) for v in line.split('UITEST_DRAG ')[1].split()[:4])
                raise_app(proc.pid, True)
                time.sleep(0.3)
                try:
                    drag(x0, y0, x1, y1)
                finally:
                    raise_app(proc.pid, False)
            elif 'UITEST_KEY ' in line:
                # UITEST_KEY <virtual-key code> [count]: key presses to the foreground window.
                parts = line.split('UITEST_KEY ')[1].split()
                vk, count = int(parts[0]), int(parts[1]) if len(parts) > 1 else 1
                win = main_window(proc.pid)
                if win:
                    user32.SetForegroundWindow(ctypes.c_void_p(win[0]))
                    time.sleep(0.2)
                fg = user32.GetForegroundWindow()
                owner = ctypes.c_ulong()
                user32.GetWindowThreadProcessId(ctypes.c_void_p(fg), ctypes.byref(owner))
                if owner.value != proc.pid:
                    print('key skipped: the app is not the foreground window')
                    continue
                for _ in range(count):
                    user32.keybd_event(vk, 0, 0, 0)
                    time.sleep(0.05)
                    user32.keybd_event(vk, 0, 2, 0)
                    time.sleep(0.15)
            elif 'UITEST_NOTE ' in line:
                print(line.split('UITEST_NOTE ', 1)[1].strip())
            elif 'UITEST_FAIL' in line:
                verdict = 'FAIL ' + line.split('UITEST_FAIL', 1)[1].strip()
            elif 'UITEST_DONE' in line:
                verdict = 'PASS'
        seen = len(lines)
        if proc.poll() is not None and verdict is None:
            time.sleep(0.5)
            lines = log.read_text(encoding='utf8', errors='replace').splitlines() if log.exists() else []
            if not any('UITEST_DONE' in l for l in lines[seen:]):
                verdict = 'FAIL app exited with code %s' % proc.returncode
    print('RESULT', verdict)
    result = 0 if verdict == 'PASS' else 1
finally:
    main.write_bytes(original)
    if proc and proc.poll() is None:
        proc.kill()
sys.exit(result)
