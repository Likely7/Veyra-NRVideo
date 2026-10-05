"""Real orphan hook/server, Qt dialog, detached replacement, and owned-child cleanup."""
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import psutil
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'rtss-restart-loop-20261004'
RUN = BASE / 'tests' / TASK / sys.argv[1]
LOGS = BASE / 'logs' / TASK / sys.argv[1]
TMP = BASE / 'tmp' / TASK / sys.argv[1]
APP = BASE / 'test-packages' / TASK / 'Veyra-2.0.3-rtssfix-NVIDIA-win64-portable'
PUBLISHED = BASE / 'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable'
RTSS = Path(sys.argv[2]) if len(sys.argv) > 2 else BASE / 'deps' / TASK / 'rtss'
installed_profiles = {}
for path in (RUN, LOGS, TMP):
    path.mkdir(parents=True, exist_ok=False)

subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/rtss-restart-control.py')], check=True)
manifest = json.loads((PUBLISHED / 'package-manifest.json').read_text(encoding='utf8'))
if not APP.exists():
    APP.mkdir(parents=True)
    for entry in manifest['files']:
        source = PUBLISHED / entry['path']
        assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], source
        dest = APP / entry['path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
    shutil.copy2(BASE / 'build' / TASK / 'veyra_qml_ui.exe', APP / 'veyra_qml_ui.exe')
    for name in ('Main.qml', 'SettingsPage.qml'):
        shutil.copy2(ROOT / 'qml/Veyra' / name, APP / 'qml/Veyra' / name)
if not RTSS.exists():
    RTSS.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(BASE / 'deps/rtss-overlay-20261002', RTSS)
if len(sys.argv) > 2:
    backup = RUN / 'installed-profiles-before'
    backup.mkdir()
    for profile in (RTSS / 'Profiles').iterdir():
        if profile.is_file():
            installed_profiles[profile.name] = profile.read_bytes()
            (backup / profile.name).write_bytes(profile.read_bytes())
shutil.copy2(BASE / 'build' / TASK / 'veyra_qml_ui.exe', APP / 'veyra_qml_ui.exe')
for name in ('Main.qml', 'SettingsPage.qml'):
    shutil.copy2(ROOT / 'qml/Veyra' / name, APP / 'qml/Veyra' / name)

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenFileMappingW.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
kernel.OpenFileMappingW.restype = ctypes.c_void_p
kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_size_t]
kernel.MapViewOfFile.restype = ctypes.c_void_p
kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]

def server_live():
    if not any(p.info['name'] and p.info['name'].lower() == 'rtss.exe'
               for p in psutil.process_iter(['name'])):
        return False
    handle = kernel.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2')
    if not handle:
        return False
    view = kernel.MapViewOfFile(handle, 4, 0, 0, 4)
    try:
        return bool(view and ctypes.c_uint32.from_address(view).value == 0x52545353)
    finally:
        if view:
            kernel.UnmapViewOfFile(view)
        kernel.CloseHandle(handle)

started = time.time()
owned = []
seen_helpers = {}
cleanup = []
assert not server_live(), 'A live external RTSS server would invalidate this isolated test'

def owned_helpers():
    hits = []
    roots = {p.pid for p in owned}
    for process in psutil.process_iter(['pid', 'ppid', 'name', 'exe', 'create_time']):
        value = process.info
        if (value['exe'] and Path(value['exe']).parent == RTSS
                and value['create_time'] >= started - 1 and value['name'].lower().startswith('rtss')
                and (value['pid'] in roots or value['ppid'] in roots
                     or seen_helpers.get(value['pid']) == value['create_time'])):
            hits.append(process)
            seen_helpers[value['pid']] = value['create_time']
    return hits

def stop_helpers():
    # Terminate only helpers from this task's private RTSS copy. Closing just
    # RTSS.exe previously left RTSSHooksLoader64.exe installed as a global hook.
    for _ in range(3):
        hits = owned_helpers()
        if not hits:
            break
        for process in sorted(hits, key=lambda p: p.name().lower() != 'rtss.exe'):
            try:
                cleanup.append({'pid': process.pid, 'name': process.name(), 'path': process.exe()})
                process.terminate()
            except psutil.NoSuchProcess:
                pass
        psutil.wait_procs(hits, timeout=3)
    assert not owned_helpers(), 'Owned RTSS helpers remain'
    assert not server_live(), 'RTSS server process still active'

def start_helper(name):
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    args = [str(RTSS / name)] + (['/i'] if 'Loader' in name else [])
    # The loader requests administrator privileges for hooking elevated apps.
    # Our own test window does not need them: keep this private helper at the
    # caller's normal integrity level, with no UAC prompt or privilege gain.
    helper_env = os.environ.copy()
    helper_env.update(__COMPAT_LAYER='RunAsInvoker', TEMP=str(TMP), TMP=str(TMP))
    process = subprocess.Popen(args, cwd=RTSS, env=helper_env, startupinfo=si,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    owned.append(process)
    deadline = time.monotonic() + 10
    while name == 'RTSS.exe' and not server_live() and time.monotonic() < deadline:
        time.sleep(.1)
    if name == 'RTSS.exe':
        assert server_live(), 'Real RTSS did not publish its live server state'
    else:
        time.sleep(.7)
        assert not server_live(), 'Orphan loader unexpectedly has a live server'

main = APP / 'qml/Veyra/Main.qml'
original = main.read_bytes()
results = []
cases = [
    ('clean', False, False, False, False, False, False, 'check'),
    ('orphan', False, True, False, False, False, False, 'check'),
    ('automatic-live', True, False, True, True, False, False, 'check'),
    ('off-live', True, False, False, False, True, False, 'check'),
    ('forced-live', True, False, False, False, False, True, 'check'),
    ('obs-live', True, False, True, False, False, False, 'check'),
    ('accepted-without-server', False, False, True, True, False, False, 'check'),
    ('accepted-off', True, False, False, False, True, False, 'check'),
    ('normal-restart', False, False, False, False, False, False, 'normal'),
    ('normal-after-accepted', False, False, True, True, False, False, 'normal'),
    ('late-real-restart', False, False, False, False, False, False, 'late'),
]

try:
    for name, live, orphan, software, compatibility, off, forced, mode in cases:
        stop_helpers()
        out, logs, tmp = RUN / name, LOGS / name, TMP / name
        for path in (out, logs, tmp):
            path.mkdir()
        profile = out / 'profile'
        profile.mkdir()
        (profile / 'qml-preferences.v1.json').write_text(json.dumps({'language': 'zh-CN', 'overlayCompat': 'off' if off else 'auto', 'uiScale': 0}), encoding='utf8')
        source = original.decode('utf8')
        loader = '\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/rtss-restart-ui.qml').as_uri()) + '; onLoaded: { '
        loader += 'item.mode=' + json.dumps(mode) + ';item.evidence=' + json.dumps(str(out)) + ';'
        loader += f'item.expectedSoftware={str(software).lower()};item.expectedCompatibility={str(compatibility).lower()};item.expectedServer={str(live).lower()};' + ' } }\n'
        at = source.rfind('}')
        main.write_text(source[:at] + loader + source[at:], encoding='utf8')
        if live:
            start_helper('RTSS.exe')
        elif orphan:
            start_helper('RTSSHooksLoader64.exe')
        env = {k: v for k, v in os.environ.items() if k.upper() not in ('QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'VEYRA_UI_RHI', 'VEYRA_TEST_IGNORE_RTSS', 'VEYRA_LOG_FILE')}
        env.update(TEMP=str(tmp), TMP=str(tmp), VEYRA_LOG_FILE=str(logs / 'app.log'), QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1')
        if forced:
            env['VEYRA_UI_RHI'] = 'd3d12'
        args = [str(APP / 'veyra_qml_ui.exe'), '--data-dir', str(profile), '--page', 'home', '--exit-after', '25000', '--reduced-motion']
        if name == 'obs-live':
            args.append('--obs-game-capture')
        if name in ('accepted-without-server', 'accepted-off', 'normal-after-accepted'):
            args.append('--overlay-compat-restart')
        with (logs / 'console.log').open('xb') as console:
            process = subprocess.Popen(args, cwd=APP, env=env, stdout=console, stderr=subprocess.STDOUT)
            owned.append(process)
            if mode == 'late':
                time.sleep(3.5)
                start_helper('RTSS.exe')
            code = process.wait(timeout=32)
        assert code == 0, (name, code)
        children = []
        if mode in ('late', 'normal'):
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                running = []
                for child in psutil.process_iter(['pid', 'exe', 'cmdline', 'create_time']):
                    value = child.info
                    if value['exe'] and Path(value['exe']) == APP / 'veyra_qml_ui.exe' and str(profile) in (value['cmdline'] or []):
                        running.append(child)
                        if value['pid'] not in [x['pid'] for x in children]:
                            children.append({'pid': value['pid'], 'args': value['cmdline']})
                if not running:
                    break
                time.sleep(.2)
            assert children and not running, (name, 'replacement not observed or did not close')
        app_logs = [logs / 'app.log'] + sorted(logs.glob('veyra-qml-*.log'))
        text = '\n'.join(p.read_text(encoding='utf8', errors='replace') for p in app_logs if p.exists())
        assert 'RTSS_RESTART_FAIL' not in text and 'RTSS_RESTART_PASS' in text, (name, text[-4000:])
        if orphan:
            assert 'RTSS hook module: activeServer=false path=' in text, 'Real orphan DLL was not injected'
            assert 'compatibility restart suggested' not in text
        if mode == 'late':
            assert text.count('RTSS_RESTART_ACCEPT') == 1
            assert 'acceptedRestart=true' in text
            assert '--overlay-compat-restart' in children[0]['args']
        if mode == 'normal':
            assert '--overlay-compat-restart' not in children[0]['args']
        image = Image.open(next(out.glob('*.png'))).convert('RGBA')
        pixel = image.getpixel((12, 80))
        if software and mode != 'normal':
            assert pixel[3] == 255 and max(pixel[:3]) < 160, pixel
        results.append({'case': name, 'exit': code, 'replacement': children, 'pixel': pixel,
                        'logFiles': [str(p) for p in app_logs if p.exists()]})
        print('RTSS CASE PASS', name, flush=True)
finally:
    main.write_bytes(original)
    stop_helpers()
    for process in owned:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    # Also collect replacement apps after a failed restart test, scoped to this
    # candidate executable and profiles created under this task's test root.
    for process in psutil.process_iter(['exe', 'cmdline']):
        value = process.info
        if value['exe'] and Path(value['exe']) == APP / 'veyra_qml_ui.exe' and any(str(RUN) in arg for arg in (value['cmdline'] or [])):
            process.terminate()
            process.wait(timeout=5)
    for name, data in installed_profiles.items():
        (RTSS / 'Profiles' / name).write_bytes(data)
        assert (RTSS / 'Profiles' / name).read_bytes() == data
    (LOGS / 'cleanup.json').write_text(json.dumps({'ownedHelpersStopped': cleanup, 'remainingOwnedHelpers': len(owned_helpers()), 'liveServer': server_live()}, indent=2), encoding='utf8')
(LOGS / 'summary.json').write_text(json.dumps({'results': results, 'ownedHelperCleanup': cleanup}, indent=2), encoding='utf8')
print('RTSS PRODUCT ACCEPTANCE PASS', len(results), LOGS)
