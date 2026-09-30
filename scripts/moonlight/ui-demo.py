"""End-to-end check of the PC streaming dialog against the mock host (no real Sunshine, no real stream).

usage: python ui-demo.py <staged-app-dir> <mockhost.exe> <out-dir>

<staged-app-dir> is a copy of the app with its Qt runtime (veyra_qml_ui.exe + qml). The script injects
ui-demo.qml into the staged Main.qml (restoring it afterwards), starts the mock host, runs the app with a
disposable data directory, feeds the PIN the dialog shows to the mock host, takes a screenshot at each
MLTEST_SHOT line and exits 0 when the app reports MLTEST_PASS.
"""
import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path

app, mock, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
here = Path(__file__).resolve().parent
out.mkdir(parents=True, exist_ok=True)
work = out / 'work'
work.mkdir(exist_ok=True)
pin_file, stop_file = work / 'pin.txt', work / 'stop.txt'
for f in (pin_file, stop_file):
    if f.exists():
        f.unlink()

main = app / 'qml/Veyra/Main.qml'
original = main.read_bytes()
source = original.decode('utf-8-sig')
snippet = (here / 'ui-demo.qml').read_text(encoding='utf8')
index = source.rfind('}')
main.write_text(source[:index] + snippet + source[index:], encoding='utf8')

user32 = ctypes.windll.user32


class RECT(ctypes.Structure):
    _fields_ = [('l', ctypes.c_long), ('t', ctypes.c_long), ('r', ctypes.c_long), ('b', ctypes.c_long)]


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rect = RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            if rect.r - rect.l > 400:
                found.append((hwnd, rect))
        return True
    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def screenshot(pid, path):
    win = find_window(pid)
    if not win:
        return False
    hwnd, rect = win
    ps = ('Add-Type -AssemblyName System.Drawing; '
          f'$b = New-Object System.Drawing.Bitmap({rect.r - rect.l},{rect.b - rect.t}); '
          '$g = [System.Drawing.Graphics]::FromImage($b); '
          f'$g.CopyFromScreen({rect.l},{rect.t},0,0,$b.Size); '
          f'$b.Save("{path}"); $g.Dispose(); $b.Dispose()')
    subprocess.run(['powershell', '-NoProfile', '-Command', ps], check=True, timeout=60)
    return True


result = 1
mock_proc = app_proc = None
try:
    mock_proc = subprocess.Popen([str(mock), '--pin-file', str(pin_file), '--stop-file', str(stop_file)],
                                 stdout=subprocess.PIPE, text=True)
    ports = mock_proc.stdout.readline().split()
    assert ports[0] == 'PORTS', ports
    address = f'127.0.0.1:{ports[1]}'
    log = out / 'app.log'
    if log.exists():
        log.unlink()
    env = os.environ.copy()
    env.update(VEYRA_LOG_FILE=str(log), VEYRA_MOONLIGHT_DATA=str(work / 'data'), VEYRA_MOONLIGHT_NO_DISCOVERY='1',
               QML_DISABLE_DISK_CACHE='1', QML2_IMPORT_PATH=str(app / 'qml'), TEMP=str(work), TMP=str(work))
    command = [str(app / 'veyra_qml_ui.exe'), '--data-dir', str(work / 'profile'), '--exit-after', '290000',
               '--ml-address=' + address]
    app_proc = subprocess.Popen(command, cwd=app, env=env, stdout=(out / 'stdio.log').open('w'), stderr=subprocess.STDOUT)
    seen = 0
    exited = False
    deadline = time.time() + 295
    verdict = None
    while time.time() < deadline and verdict is None:
        time.sleep(0.2)
        if app_proc.poll() is not None and not log.exists():
            break
        lines = log.read_text(encoding='utf8', errors='replace').splitlines() if log.exists() else []
        for line in lines[seen:]:
            if 'MLTEST_PIN ' in line:
                pin = line.split('MLTEST_PIN ')[1].strip()
                time.sleep(0.4)
                screenshot(app_proc.pid, out / '03-pairing.png')
                pin_file.write_text(pin)
                print('PIN from the dialog handed to the mock host:', pin)
            elif 'MLTEST_SHOT ' in line:
                name = line.split('MLTEST_SHOT ')[1].strip()
                time.sleep(0.6)
                ok = screenshot(app_proc.pid, out / f'{name}.png')
                print('shot', name, 'ok' if ok else 'NO WINDOW')
            elif 'MLTEST_STREAM' in line:
                print(line.split('mltest', 1)[-1].strip())
            elif 'MLTEST_FAIL' in line:
                verdict = 'FAIL ' + line.split('MLTEST_FAIL', 1)[1].strip()
            elif 'MLTEST_DONE' in line:
                verdict = 'PASS'
        seen = len(lines)
        if app_proc.poll() is not None and verdict is None:
            # read the log once more: the last lines may have been written just before the exit
            if exited:
                verdict = 'FAIL app exited early with code %s' % app_proc.returncode
            exited = True
    print('RESULT', verdict)
    result = 0 if verdict == 'PASS' else 1
finally:
    main.write_bytes(original)
    stop_file.write_text('x')
    if app_proc and app_proc.poll() is None:
        time.sleep(2)
        if app_proc.poll() is None:
            app_proc.kill()
    if mock_proc:
        try:
            mock_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mock_proc.kill()
sys.exit(result)
