"""Run one serial real-player baseline/candidate experiment and preserve raw evidence."""
from pathlib import Path
import csv
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
import psutil

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
PACKAGE = BASE / 'test-packages/playback-smoothness-20261004/Veyra-2.0.3-smoothfix-NVIDIA-win64-portable'
BUILD = BASE / 'build' / TASK
SOURCES = {'M1': BASE / 'tests/hotfix-2.0.0-20261002/media/gta6-1080p30-60s.mp4'}
CONFIGS = {
    'S1': {'layers': 1, 'policies': [1], 'temporal': False},
    # The current UI has fixed sizes, not 50%; label this actual 720p policy.
    'S2-720': {'layers': 1, 'policies': [3], 'temporal': False},
    'S3': {'layers': 1, 'policies': [0], 'temporal': False, 'sr': True},
    'S4': {'layers': 2, 'policies': [0, 0], 'temporal': False, 'sr': True, 'fg': True},
    'S5-existing': {'layers': 3, 'policies': [2, 3, 1], 'temporal': False},
}

def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def copy_dependency(src, dst):
    if Path(src).suffix.lower() in {'.dll', '.hsaco', '.ptx', '.onnx', '.f16', '.bin', '.cubin'}:
        os.link(src, dst)
    else:
        shutil.copy2(src, dst)
    return dst

def gpu_query():
    cmd = ['nvidia-smi', '--query-gpu=name,driver_version,utilization.gpu,memory.used,memory.total,clocks.current.graphics,clocks.current.memory,power.draw', '--format=csv,noheader,nounits']
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
    if p.returncode:
        return {'queryError': p.returncode}
    values = [v.strip() for v in p.stdout.strip().split(',')]
    return dict(zip(('name','driver','gpuPercent','memoryMiB','memoryTotalMiB','graphicsMHz','memoryMHz','powerW'), values))

def assert_gpu_tests_idle(allowed_pids=()):
    # Timing must never overlap another owned GPU test. A future competition
    # test explicitly exempts only its own load process at its call site.
    prefix=str(BASE/'tests'/TASK).replace('\\','/').lower()+'/'
    allowed=set(allowed_pids)
    for pid in allowed:
        owned=psutil.Process(pid)
        exe=owned.exe().replace('\\','/').lower()
        assert exe.startswith(prefix) and Path(exe).name=='veyra_nr_gpu_competition_load.exe', 'Only this task competition fixture may overlap'
    active=[]
    for proc in psutil.process_iter(['pid','name','exe']):
        try:
            exe=(proc.info['exe'] or '').replace('\\','/').lower()
            if proc.pid not in allowed and exe.startswith(prefix) and (proc.info['name'] or '').lower().startswith('veyra_'):
                active.append({'pid':proc.pid,'exe':exe})
        except psutil.Error:
            continue
    assert not active, f'Another owned GPU test is running: {active}'

def set_owned_gpu_priority(proc, priority):
    # Native API changes only the child handle returned by our own Popen.
    # No elevation, driver settings or other process priority changes.
    import ctypes
    from ctypes import wintypes
    gdi=ctypes.WinDLL('gdi32',use_last_error=True)
    get=gdi.D3DKMTGetProcessSchedulingPriorityClass
    get.argtypes=[wintypes.HANDLE,ctypes.POINTER(ctypes.c_int)];get.restype=wintypes.LONG
    set_value=gdi.D3DKMTSetProcessSchedulingPriorityClass
    set_value.argtypes=[wintypes.HANDLE,ctypes.c_int];set_value.restype=wintypes.LONG
    before=ctypes.c_int(-1);after=ctypes.c_int(-1)
    # WDDM has no scheduling record before the child creates its GPU device.
    # Wait for the actual Get to succeed, retaining every failed status.
    deadline=time.monotonic()+30;attempts=[]
    while True:
        a=get(int(proc._handle),ctypes.byref(before));attempts.append(hex(a&0xffffffff))
        if a==0 or proc.poll() is not None or time.monotonic()>=deadline:break
        time.sleep(.1)
    b=set_value(int(proc._handle),priority);c=get(int(proc._handle),ctypes.byref(after))
    return {'pid':proc.pid,'beforeStatus':hex(a&0xffffffff),'beforeClass':before.value,
            'setStatus':hex(b&0xffffffff),'afterStatus':hex(c&0xffffffff),'afterClass':after.value,'getAttempts':attempts,
            'applied':a==b==c==0 and after.value==priority}

def stage(variant,stageLabel=None):
    assert stageLabel is None or stageLabel.replace('-','').isalnum()
    path = BASE / 'tests' / TASK / ('app-' + variant+('-'+stageLabel if stageLabel else ''))
    if path.exists():
        assert digest(path/'veyra_qml_ui.exe')==digest(BUILD/variant/'veyra_qml_ui.exe'), 'Existing stage executable is stale; use a fresh variant'
        return path
    shutil.copytree(PACKAGE, path, copy_function=copy_dependency,
                    ignore=shutil.ignore_patterns('*.log', 'logs', 'user-data-2.0.0', '*.dmp'))
    assert digest(BUILD / variant / 'veyra_qml_ui.exe')
    shutil.copy2(BUILD / variant / 'veyra_qml_ui.exe', path / 'veyra_qml_ui.exe')
    if variant != 'A':
        shutil.copytree(ROOT / 'qml', path / 'qml', dirs_exist_ok=True)
    shutil.copy2(ROOT / 'scripts/perf/nr-probe.qml', path / 'qml/Veyra/NrPerfProbe.qml')
    main = path / 'qml/Veyra/Main.qml'
    text = main.read_text(encoding='utf-8'); at = text.rfind('}')
    text = text[:at] + '\n Loader { source: "NrPerfProbe.qml"; onLoaded: { item.media=veyra.testOptions.perfMedia; item.config=JSON.parse(veyra.testOptions.perfConfig); item.seconds=veyra.testOptions.perfSeconds } }\n' + text[at:]
    # testOptions has no arbitrary CLI arguments; make only this owned loader
    # read a fixed per-run JSON via Qt's supported XMLHttpRequest API instead.
    text = text.replace('item.media=veyra.testOptions.perfMedia; item.config=JSON.parse(veyra.testOptions.perfConfig); item.seconds=veyra.testOptions.perfSeconds',
                        'const r=new XMLHttpRequest(); r.open("GET", "' + (path / 'perf-run.json').as_uri() + '", false); r.send(); const c=JSON.parse(r.responseText); item.media=c.media; item.config=c.config; item.seconds=c.seconds')
    main.write_text(text, encoding='utf-8')
    return path

def run(variant, material, setting, label, seconds, gpuPriority=None, allowedGpuPids=(),testEnv=None,stageLabel=None,onPoll=None):
    assert_gpu_tests_idle(allowedGpuPids)
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/perf/nr-control.py'), 'guard'], check=True)
    app = stage(variant,stageLabel)
    media = SOURCES.get(material, BASE / 'tests/perf-matrix/media' / (material + '.mkv'))
    assert media.is_file(), media
    logs = BASE / 'logs' / TASK / label
    profile = BASE / 'tests' / TASK / label / 'profile'
    tmp = BASE / 'tmp' / TASK / label
    for path in (logs, profile, tmp):
        path.mkdir(parents=True, exist_ok=False)
    config = CONFIGS[setting]
    (app / 'perf-run.json').write_text(json.dumps({'media': str(media).replace('\\', '/'), 'config': config,
                                                 'seconds': seconds}), encoding='utf-8')
    (profile / 'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat': 'off'}), encoding='utf-8')
    env = os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith(('VEYRA_', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND')):
            env.pop(key)
    env.update(TEMP=str(tmp), TMP=str(tmp), VEYRA_LOG_FILE=str(logs / 'player.log'),
               QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', QML_XHR_ALLOW_FILE_READ='1',
               VEYRA_VERBOSE_FRAME_LOGS='1')
    if testEnv:
        assert all(key.startswith('VEYRA_TEST_') for key in testEnv)
        env.update(testEnv)
    before = gpu_query()
    receipt = {'variant': variant, 'material': material, 'setting': setting, 'config': config,
               'testEnv':testEnv or {},
               'sourceSha256': digest(media), 'exeSha256': digest(app / 'veyra_qml_ui.exe'),
               'runtimeSha256': digest(app / 'runtime/experimental/nvngx_dlssnr.dll'),
               'beforeGpu': before, 'secondsRequested': seconds, 'measurement': 'GPU timestamps and CPU submission, not display scanout',
               'sourceCommit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip()}
    start = time.monotonic();receipt['startUtc']=time.time()
    with (logs / 'console.log').open('xb') as out:
        proc = subprocess.Popen([str(app / 'veyra_qml_ui.exe'), '--data-dir', str(profile),
                                 '--page', 'pro', '--size', '1280x800', '--exit-after', '270000'],
                                cwd=app, env=env, stdout=out, stderr=subprocess.STDOUT)
        receipt['pid'] = proc.pid
        if gpuPriority is not None:
            receipt['gpuPriority']=set_owned_gpu_priority(proc,gpuPriority)
        meter = psutil.Process(proc.pid); meter.cpu_percent(None)
        samples = []
        try:
            while proc.poll() is None and time.monotonic()-start < 280:
                if onPoll is not None:onPoll(proc,logs/'player.log')
                try:
                    cpu, rss = meter.cpu_percent(None), meter.memory_info().rss
                except psutil.Error:
                    break
                samples.append({'elapsed': time.monotonic()-start, 'cpuPercentOneCoreScale': cpu, 'rssBytes': rss,
                                'gpu': gpu_query()})
                time.sleep(1)
            if proc.poll() is None:
                proc.kill(); proc.wait(timeout=8); rc = 124
            else:
                rc = proc.returncode
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait(timeout=8)
    receipt.update(exitCode=rc, wallSeconds=time.monotonic()-start, afterGpu=gpu_query())
    text = (logs / 'player.log').read_text(encoding='utf-8', errors='replace')
    receipt['passed'] = rc == 0 and 'NR_PERF_PASS' in text and 'NR_PERF_FAIL' not in text and not any(s in text for s in ('ReferenceError:', 'TypeError:', '[ERROR]', '[FATAL]'))
    # Each line contains rolling statistics. Summarise their steady observations,
    # never call their median an aggregate percentile of all individual frames.
    rows = []
    steady = False
    for line in text.splitlines():
        if 'NR_PERF_SAMPLE ' in line:
            try:
                sample = json.loads(line.split('NR_PERF_SAMPLE ',1)[1])
                steady = sample['ready'] and 10 <= sample['position'] <= 48
            except (ValueError, KeyError):
                pass
        if '[player-timing]' in line and steady:
            row = {k: float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
            rows.append(row)
    keys = sorted(set(k for row in rows for k in row))
    summary = {key: {'medianOfRollingObservations': statistics.median(row[key] for row in rows if key in row),
                     'min': min(row[key] for row in rows if key in row),
                     'max': max(row[key] for row in rows if key in row)} for key in keys}
    receipt['steadyObservationCount'] = len(rows)
    receipt['summary'] = summary
    receipt['passed'] = receipt['passed'] and len(rows) >= 10
    (logs / 'meters.json').write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding='utf-8')
    (logs / 'result.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding='utf-8')
    with (logs / 'timing.csv').open('x', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=keys); writer.writeheader(); writer.writerows(rows)
    print(json.dumps({key: receipt[key] for key in ('variant','material','setting','exitCode','passed','steadyObservationCount','summary')}, ensure_ascii=False), flush=True)
    if not receipt['passed']:
        print('\n'.join(text.splitlines()[-30:]), flush=True)
        raise SystemExit(1)

if __name__ == '__main__':
    variant, material, setting, label = sys.argv[1:5]
    assert variant.replace('-', '').isalnum() and label.replace('-', '').isalnum()
    assert setting in CONFIGS
    run(variant, material, setting, label, int(sys.argv[5]) if len(sys.argv)>5 else 50)
