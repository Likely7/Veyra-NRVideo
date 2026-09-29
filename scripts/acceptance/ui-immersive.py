"""Exercise the daily immersive sub-state in the real app.

Without media (default): entry/exit through the bar button, Esc and H, the
overlay layout (video owns the client), borderless edge hit-testing through
the video, bar-background caption drag, and the no-media hold (the bar never
hides while nothing is open). With a media path (an image is enough): the
3 s entry grace, auto-hide, bottom-band reveal, motion outside the band not
revealing, and the fullscreen round trip with the same rules.

  python ui-immersive.py <veyra.exe> <out dir> [media]

The media case moves the real mouse cursor (SetCursorPos); run it unattended.
"""
import ctypes as c
import ctypes.wintypes as w
import json
import os
from pathlib import Path
import subprocess
import sys
import time

exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
media = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
out.mkdir(parents=True, exist_ok=True)
u = c.WinDLL('user32', use_last_error=True)
# Keep window rectangles and SetCursorPos in the same physical-pixel space,
# matching the app and the other native UI acceptance scripts on scaled displays.
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.IsWindowVisible.argtypes = [w.HWND]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.MapWindowPoints.argtypes = [w.HWND, w.HWND, c.POINTER(w.POINT), w.UINT]
u.GetDpiForWindow.argtypes = [w.HWND]
u.GetDpiForWindow.restype = w.UINT
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SetCursorPos.argtypes = [c.c_int, c.c_int]
u.GetCursorPos.argtypes = [c.POINTER(w.POINT)]
u.MonitorFromWindow.argtypes = [w.HWND, w.DWORD]
u.MonitorFromWindow.restype = w.HANDLE
class MONITORINFO(c.Structure):
    _fields_ = [('cbSize', w.DWORD), ('rcMonitor', w.RECT), ('rcWork', w.RECT), ('dwFlags', w.DWORD)]
u.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(MONITORINFO)]

WM_NULL, WM_CLOSE, WM_NCHITTEST, WM_KEYDOWN, WM_COMMAND, WM_MOUSEMOVE = 0x0, 0x10, 0x84, 0x100, 0x111, 0x200
VK_ESCAPE, VK_F11, VK_H = 0x1B, 0x7A, 0x48
HTTRANSPARENT, HTCLIENT, HTCAPTION, HTLEFT, HTTOP, HTBOTTOM, HTBOTTOMRIGHT = (1 << 64) - 1, 1, 2, 10, 12, 15, 17
IMMERSIVE, FULLSCREEN, MODE_SWITCH, VIDEO = 250, 120, 220, 1000

result = {'passed': False, 'media': str(media) if media else None, 'checks': []}
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str(out / 'app.log')
args = [str(exe), '--smoke-view', 'daily', '--smoke-seconds', '90']
args += [str(media), '--no-nr', '--no-sr'] if media else ['--smoke-empty']
with (out / 'stdout.txt').open('w') as stdout, (out / 'stderr.txt').open('w') as stderr:
    process = subprocess.Popen(args, env=env, stdout=stdout, stderr=stderr)
    try:
        found = []
        @visit_type
        def visit(hwnd, unused):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == 'VeyraApp':
                found.append(hwnd)
            return True
        until = time.monotonic() + 15
        while not found and time.monotonic() < until:
            u.EnumWindows(visit, 0)
            time.sleep(.1)
        assert found, 'main window missing'
        main = found[0]
        bars = []
        @visit_type
        def child(hwnd, unused):
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, 128)
            if name.value == 'VeyraPlaybackBar':
                bars.append(hwnd)
            return True
        u.EnumChildWindows(main, child, 0)
        assert len(bars) == 1, 'playback bar missing'
        bar, button, video = bars[0], u.GetDlgItem(main, IMMERSIVE), u.GetDlgItem(main, VIDEO)
        assert button and video, 'immersive button or video surface missing'
        # The window is enumerable before ShowWindow and before the first
        # layout has run; wait for the shell to actually be on screen.
        until = time.monotonic() + 15
        while not (u.IsWindowVisible(main) and u.IsWindowVisible(button)) and time.monotonic() < until:
            time.sleep(.1)
        result['startup'] = {'mainVisible': bool(u.IsWindowVisible(main)), 'buttonVisible': bool(u.IsWindowVisible(button))}

        def send(hwnd, msg, wp=0, lp=0):
            reply = c.c_size_t()
            assert u.SendMessageTimeoutW(hwnd, msg, wp, lp, 2, 4000, c.byref(reply)), 'UI timeout'
            return reply.value
        def post(msg, wp=0, lp=0):
            # Posted input is pre-translated by the app's own message loop
            # (keys, band reveal); sent messages bypass it, so sync by waiting.
            u.PostMessageW(main, msg, wp, lp)
            time.sleep(.05)
            send(main, WM_NULL)
        def settle(condition, seconds=2.0):
            until = time.monotonic() + seconds
            while not condition() and time.monotonic() < until:
                time.sleep(.05)
            return condition()
        def text(hwnd):
            buffer = c.create_unicode_buffer(256)
            u.GetWindowTextW(hwnd, buffer, 256)
            return buffer.value
        def client():
            r = w.RECT()
            u.GetClientRect(main, c.byref(r))
            return r
        def child_rect(hwnd):
            r = w.RECT()
            u.GetWindowRect(hwnd, c.byref(r))
            points = (w.POINT * 2)(w.POINT(r.left, r.top), w.POINT(r.right, r.bottom))
            u.MapWindowPoints(None, main, points, 2)
            return points[0].x, points[0].y, points[1].x, points[1].y
        def window_rect():
            r = w.RECT()
            u.GetWindowRect(main, c.byref(r))
            return r
        def dip(v):
            return round(v * u.GetDpiForWindow(main) / 96)
        def lparam(x, y):
            return (y & 0xFFFF) << 16 | (x & 0xFFFF)
        def hit(hwnd, x, y):
            return send(hwnd, WM_NCHITTEST, 0, lparam(x, y))
        def check(ok, label):
            result['checks'].append({'check': label, 'ok': bool(ok)})
            if not ok:
                pointer = w.POINT()
                pointer_read = u.GetCursorPos(c.byref(pointer))
                result['state'] = {'button': text(button), 'buttonVisible': bool(u.IsWindowVisible(button)), 'barVisible': bool(u.IsWindowVisible(bar)),
                                   'video': child_rect(video), 'client': (client().right, client().bottom), 'mode': text(u.GetDlgItem(main, MODE_SWITCH)),
                                   'dpi': u.GetDpiForWindow(main), 'cursor': [pointer.x, pointer.y] if pointer_read else None}
            assert ok, label
        def move_cursor(x, y):
            if not u.SetCursorPos(x, y):
                raise c.WinError(c.get_last_error())
            time.sleep(.05)
            post(WM_MOUSEMOVE, 0, lparam(x, y))
        def video_fills_client():
            x0, y0, x1, y1 = child_rect(video)
            r = client()
            return (x0, y0, x1, y1) == (0, 0, r.right, r.bottom)
        def video_leaves_bar():
            x0, y0, x1, y1 = child_rect(video)
            r = client()
            return (x0, y0, x1) == (0, 0, r.right) and abs(y1 - (r.bottom - dip(88))) <= 1

        if media:
            # Opening an image lands in professional mode; the toggle is a daily control.
            until = time.monotonic() + 20
            while text(u.GetDlgItem(main, MODE_SWITCH)) != '返回日常模式' and time.monotonic() < until:
                time.sleep(.2)
            check(text(u.GetDlgItem(main, MODE_SWITCH)) == '返回日常模式', 'image opened into professional mode')
            send(main, WM_COMMAND, MODE_SWITCH)
            time.sleep(.6)
        check(text(button) == '沉浸' and u.IsWindowVisible(button), 'daily bar shows the immersive entry')
        check(video_leaves_bar(), 'normal daily video stops above the docked bar')
        wr = window_rect()
        top_edge = (wr.left + (wr.right - wr.left) // 2, wr.top + 2)
        check(hit(video, *top_edge) == HTTRANSPARENT and hit(main, *top_edge) == HTTOP, 'normal daily: top edge over the video resizes')

        send(main, WM_COMMAND, IMMERSIVE)
        check(text(button) == '退出沉浸' and u.IsWindowVisible(button), 'entering immersive flips the toggle')
        check(video_fills_client(), 'immersive video owns the whole client')
        wr = window_rect()
        left_edge = (wr.left + 2, wr.top + (wr.bottom - wr.top) // 2)
        corner = (wr.right - 2, wr.bottom - 2)
        bottom_edge = (wr.left + (wr.right - wr.left) // 2, wr.bottom - 2)
        centre = (wr.left + (wr.right - wr.left) // 2, wr.top + (wr.bottom - wr.top) // 2)
        check(hit(video, *left_edge) == HTTRANSPARENT and hit(main, *left_edge) == HTLEFT, 'left edge over the video resizes')
        check(hit(video, *corner) == HTTRANSPARENT and hit(main, *corner) == HTBOTTOMRIGHT, 'bottom-right corner over the video resizes')
        check(hit(video, *bottom_edge) == HTTRANSPARENT and hit(main, *bottom_edge) == HTBOTTOM, 'bottom edge over the video resizes')
        check(hit(video, *centre) == HTCLIENT, 'video centre stays a client hit (drag handled by the surface)')
        bar_point = (wr.left + (wr.right - wr.left) // 2, wr.bottom - dip(44))
        check(hit(bar, *bar_point) == HTTRANSPARENT and hit(main, *bar_point) == HTCAPTION, 'bar background drags the window')

        # Sizing from the bottom: the pointer sits in the band, yet the bar must
        # get out of the way so the picture edge can be aligned. SC_SIZE with
        # WMSZ_BOTTOM enters DefWindowProc's modal loop; arrows grow the window.
        before = window_rect()
        check(u.IsWindowVisible(button), 'bar visible before sizing')
        u.PostMessageW(main, 0x112, 0xF006, 0)
        time.sleep(.3)
        send(main, WM_NULL)
        check(not u.IsWindowVisible(button) and not u.IsWindowVisible(bar), 'bar hides while the bottom edge is being sized')
        for i in range(6):
            u.PostMessageW(main, WM_KEYDOWN, 0x28, 0)
            time.sleep(.06)
        send(main, WM_NULL)
        check(not u.IsWindowVisible(button), 'bar stays hidden during the sizing loop')
        u.PostMessageW(main, WM_KEYDOWN, 0x0D, 0)
        time.sleep(.5)
        send(main, WM_NULL)
        after = window_rect()
        check((after.bottom - after.top) != (before.bottom - before.top), 'sizing loop changed the window height')
        wr = window_rect()
        centre = (wr.left + (wr.right - wr.left) // 2, wr.top + (wr.bottom - wr.top) // 2)
        if media:
            move_cursor(centre[0], wr.bottom - 30)
            time.sleep(.3)
            check(not u.IsWindowVisible(button), 'after sizing the band does not reveal until it is re-entered')
            move_cursor(*centre)
            move_cursor(centre[0], wr.bottom - 30)
            check(settle(lambda: u.IsWindowVisible(button)), 're-entering the band after sizing reveals the bar')
            move_cursor(*centre)
            time.sleep(2.4)
            # The 100 ms UI timer may still be committing its layout when the
            # sleep ends. Observe completion instead of racing that redraw;
            # exact idle/grace boundaries are covered by UiContractTests.
            check(settle(lambda: not u.IsWindowVisible(button)), 'bar hides again after the post-sizing reveal')
        else:
            check(settle(lambda: u.IsWindowVisible(button) and u.IsWindowVisible(bar)), 'nothing open: the bar comes straight back after sizing')
        u.PostMessageW(main, 0x112, 0xF006, 0)
        time.sleep(.3)
        for i in range(6):
            u.PostMessageW(main, WM_KEYDOWN, 0x26, 0)
            time.sleep(.06)
        u.PostMessageW(main, WM_KEYDOWN, 0x0D, 0)
        time.sleep(.5)
        send(main, WM_NULL)
        wr = window_rect()
        centre = (wr.left + (wr.right - wr.left) // 2, wr.top + (wr.bottom - wr.top) // 2)
        # Keyboard sizing steps clamp at the screen edge, so the two loops need
        # not cancel out exactly; the second one only has to shrink again.
        check((wr.bottom - wr.top) < (after.bottom - after.top), 'second sizing loop shrinks the window again')
        if media:
            move_cursor(*centre)
            move_cursor(centre[0], wr.bottom - 30)
            check(settle(lambda: u.IsWindowVisible(button)), 'bar available again after the second sizing loop')

        if not media:
            time.sleep(4.8)
            check(u.IsWindowVisible(button) and u.IsWindowVisible(bar), 'nothing open: the immersive bar never hides')
        else:
            move_cursor(*centre)
            time.sleep(4.8)
            check(settle(lambda: not u.IsWindowVisible(button) and not u.IsWindowVisible(bar)), 'bar hides after the entry grace with the pointer parked on the video')
            for i in range(4):
                move_cursor(centre[0] + i * 3, centre[1] + i * 2)
            check(not u.IsWindowVisible(button), 'motion outside the bottom band does not reveal')
            move_cursor(centre[0], wr.bottom - 30)
            check(settle(lambda: u.IsWindowVisible(button) and u.IsWindowVisible(bar)), 'entering the bottom band reveals the bar')
            move_cursor(*centre)
            time.sleep(2.4)
            check(settle(lambda: not u.IsWindowVisible(button)), 'leaving the band hides again after the idle time')
            post(WM_KEYDOWN, 0x27)  # Right arrow: transport feedback reveals
            check(settle(lambda: u.IsWindowVisible(button)), 'a transport key reveals the bar')

        send(main, WM_KEYDOWN, VK_ESCAPE)
        check(text(button) == '沉浸' and video_leaves_bar(), 'Esc leaves windowed immersive')
        post(WM_KEYDOWN, VK_H)
        check(settle(lambda: text(button) == '退出沉浸' and video_fills_client()), 'H re-enters immersive through the message loop')

        send(main, WM_KEYDOWN, VK_F11)
        info = MONITORINFO(c.sizeof(MONITORINFO))
        u.GetMonitorInfoW(u.MonitorFromWindow(main, 2), c.byref(info))
        wr = window_rect()
        check((wr.left, wr.top, wr.right, wr.bottom) == (info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom), 'F11 covers the monitor')
        check(text(button) == '退出沉浸' and u.IsWindowVisible(button) and u.IsWindowVisible(u.GetDlgItem(main, MODE_SWITCH)), 'immersive fullscreen keeps the daily bar with the toggle')
        if media:
            mon_centre = ((info.rcMonitor.left + info.rcMonitor.right) // 2, (info.rcMonitor.top + info.rcMonitor.bottom) // 2)
            move_cursor(*mon_centre)
            time.sleep(4.8)
            check(settle(lambda: not u.IsWindowVisible(button)), 'fullscreen immersive hides after the grace')
            for i in range(4):
                move_cursor(mon_centre[0] + i * 3, mon_centre[1] + i * 2)
            check(not u.IsWindowVisible(button), 'fullscreen immersive ignores motion outside the band')
            move_cursor(mon_centre[0], info.rcMonitor.bottom - 30)
            check(settle(lambda: u.IsWindowVisible(button)), 'fullscreen immersive reveals from the bottom band')
        send(main, WM_KEYDOWN, VK_ESCAPE)
        wr = window_rect()
        check((wr.right - wr.left, wr.bottom - wr.top) != (info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top), 'Esc leaves fullscreen first')
        check(text(button) == '退出沉浸' and video_fills_client(), 'windowed immersive survives the fullscreen round trip')
        send(main, WM_KEYDOWN, VK_ESCAPE)
        check(text(button) == '沉浸' and video_leaves_bar(), 'second Esc restores the docked daily bar')
        send(main, WM_CLOSE)
        assert process.wait(timeout=15) == 0, 'clean exit'
        result['passed'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        result['exitCode'] = process.returncode
        log = out / 'app.log'
        if log.exists():
            lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
            result['immersiveLog'] = [line for line in lines if 'ui-immersive' in line][:40]
        (out / 'result.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
        print(json.dumps(result, indent=2, ensure_ascii=False))
