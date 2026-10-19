"""Sponsor page in the real app (2026-10-19): the home coffee line, 设置 → 赞助作者, every link.

usage: py -3.11 -B scripts/acceptance/sponsor-gui.py APP [one]

APP is a staged app whose executable and qml/Veyra come from this build. Per case
(language, window size, scale) the app runs with a private profile and a private copy
of Main.qml that loads sponsor-gui.qml (restored afterwards). That QML prints window
pixel points; this script posts the clicks to the window (the user's cursor never
moves), so the real TapHandlers receive them. VEYRA_TEST_SPONSOR_NO_BROWSER makes the
bridge log each address instead of opening a browser: the log must name all six pages
in click order. Each code card is saved as shown and its QR must decode to the same
payment code as the bundled image (payloads are compared, never printed).
"""
from pathlib import Path
import ctypes, ctypes.wintypes as wt, json, os, re, subprocess, sys, threading, time
import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = 'claude-sponsor-20261019'
APP = Path(sys.argv[1])
LOG = BASE / 'logs' / TASK / ('gui-' + time.strftime('%Y%m%d-%H%M%S')); LOG.mkdir(parents=True)
LINKS = [('bilibili', 'https://space.bilibili.com/19500532?spm_id_from=333.1007.0.0'), ('douyin', 'https://v.douyin.com/9s06Bf2Inqc/'),
         ('x', 'https://x.com/likely2002'), ('github', 'https://github.com/Likely7'),
         ('youtube', 'https://www.youtube.com/@Likely7Ai'), ('kofi', 'https://ko-fi.com/likely7')]
user32 = ctypes.windll.user32
detector = cv2.QRCodeDetector()


def main_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        owner = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(h):
            r = wt.RECT(); user32.GetWindowRect(h, ctypes.byref(r))
            found.append((h, (r.right - r.left) * (r.bottom - r.top)))
        return True
    user32.EnumWindows(each, 0)
    return max(found, key=lambda f: f[1])[0] if found else None


def click(hwnd, x, y):
    at = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd, 0x0200, 0, at)   # WM_MOUSEMOVE
    time.sleep(0.15)
    user32.PostMessageW(hwnd, 0x0201, 1, at)   # WM_LBUTTONDOWN
    time.sleep(0.06)
    user32.PostMessageW(hwnd, 0x0202, 0, at)   # WM_LBUTTONUP


def code(path):
    if not path.exists():
        return ''
    image = cv2.cvtColor(np.array(Image.open(path).convert('RGB')), cv2.COLOR_RGB2BGR)
    return detector.detectAndDecode(image)[0]


expected = {n: code(ROOT / 'qml/Veyra/sponsor' / f'{n}.png') for n in ('wechat', 'alipay')}
assert all(expected.values())
for proc in subprocess.run(['tasklist', '/fo', 'csv', '/nh'], capture_output=True, text=True).stdout.splitlines():
    assert not proc.lower().startswith('"veyra'), 'another Veyra is running: ' + proc

main = APP / 'qml/Veyra/Main.qml'
saved = main.read_bytes()
# The last case draws the interface with Qt's software renderer (OBS 游戏捕获兼容, the same
# path as the RTSS / overlay compatibility mode).
cases = [('zh-CN', 1280, 900, '1', False), ('en', 1280, 900, '1', False), ('ja', 900, 800, '1', False),
         ('zh-TW', 900, 800, '1', False), ('en', 900, 800, '1.5', False), ('zh-CN', 1280, 900, '1', True)]
if sys.argv[2:] == ['one']:
    cases = cases[:1]
elif sys.argv[2:] == ['software']:
    cases = cases[-1:]
results = []
try:
    for language, width, height, scale, software in cases:
        name = f'{language}-{width}-{scale}' + ('-software' if software else '')
        evidence = LOG / name; evidence.mkdir()
        tmp = BASE / 'tmp' / TASK / LOG.name / name; tmp.mkdir(parents=True)
        profile = BASE / 'tests' / TASK / 'profiles' / LOG.name / name; profile.mkdir(parents=True)
        (profile / 'qml-preferences.v1.json').write_text(json.dumps({
            'language': language, 'autoResume': False, 'rememberPosition': False, 'prewarmEnhancement': False,
            'gpuPriority': 'normal', 'subtitleEnabled': False, 'obsGameCapture': software}), encoding='utf8')
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_'))}
        env.update(PATH=str(APP) + os.pathsep + env['PATH'], TEMP=str(tmp), TMP=str(tmp), CUDA_CACHE_PATH=str(tmp / 'cuda'),
                   QML_DISK_CACHE_PATH=str(tmp / 'qml-cache'), QT_FORCE_STDERR_LOGGING='1', QT_SCALE_FACTOR=scale,
                   VEYRA_LOG_FILE=str(evidence / 'engine.log'), VEYRA_TEST_SPONSOR_NO_BROWSER='1')
        loader = ('\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/sponsor-gui.qml').as_uri())
                  + '; onLoaded: { item.appRoot = root; item.settings = settingsPage; item.evidence = '
                  + json.dumps(evidence.as_posix(), ensure_ascii=False) + ' } }\n')
        text = saved.decode('utf8'); at = text.rfind('}'); assert at > 0
        main.write_bytes((text[:at] + loader + text[at:]).encode('utf8'))
        command = [str(APP / 'veyra_qml_ui.exe'), '--data-dir', str(profile), '--page', 'home', '--size', f'{width}x{height}',
                   '--exit-after', '90000']
        began = time.monotonic(); clicks = []
        proc = subprocess.Popen(command, cwd=APP, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        console = []

        def read():
            for raw in proc.stdout:
                line = raw.decode('utf8', errors='replace').rstrip()
                console.append(line)
                m = re.search(r'SPONSOR_UI_CLICK (\w+) (-?\d+) (-?\d+)', line)
                if m:
                    hwnd = main_window(proc.pid)
                    clicks.append(m.group(1))
                    if hwnd: click(hwnd, int(m.group(2)), int(m.group(3)))
        reader = threading.Thread(target=read, daemon=True); reader.start()
        try:
            proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait()
        reader.join(5)
        (evidence / 'console.log').write_text('\n'.join(console), encoding='utf8')
        engine = (evidence / 'engine.log').read_text(encoding='utf8', errors='replace') if (evidence / 'engine.log').exists() else ''
        errors = [l for l in console if re.search(r'SPONSOR_UI_FAIL|ReferenceError:|TypeError:|Binding loop|Cannot assign|'
                                                  r'(Sponsor\w*|HomePage|SettingsPage|Main)\.qml:\d+', l)]
        opened = re.findall(r'test url (\S+)', engine)
        checks = {
            'pass marker': any('SPONSOR_UI_PASS' in l for l in console),
            'no qml errors': not errors,
            'clicks in order': clicks == ['home'] + [n for n, _ in LINKS] + ['about'],
            'home link logged': 'home link' in engine,
            'six pages in order': opened == [u for _, u in LINKS],
            'codes decode as shown': all(code(evidence / f'code-{n}.png') == v for n, v in expected.items()),
            'interface renderer': ('software UI' in engine) == software,
        }
        row = {'case': name, 'exit': proc.returncode, 'seconds': round(time.monotonic() - began, 1), 'checks': checks,
               'passed': all(checks.values()), 'errors': errors[:20], 'clicks': clicks, 'opened': opened, 'profile': str(profile),
               'events': [l for l in console if 'SPONSOR_UI_' in l]}
        results.append(row)
        print(json.dumps({k: row[k] for k in ('case', 'exit', 'seconds', 'checks', 'passed')}, ensure_ascii=False), flush=True)
        if not row['passed']:
            print('\n'.join(row['errors'] + [l for l in console if 'SPONSOR_UI_FAIL' in l]), flush=True)
            break
finally:
    main.write_bytes(saved)
receipt = {'app': str(APP), 'head': subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip(),
           'results': results, 'passed': len(results) == len(cases) and all(r['passed'] for r in results)}
(LOG / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=1), encoding='utf8')
print('SPONSOR GUI', 'PASS' if receipt['passed'] else 'FAIL', LOG, flush=True)
raise SystemExit(not receipt['passed'])
