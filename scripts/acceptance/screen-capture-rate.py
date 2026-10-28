"""Window-capture rate, real app (claude-capture-fps-audio-20261027).

A probe window presents exactly 60 FPS (tmp/<task>/bench/wgc_rate_probe.exe source
free60); the player captures it with WGC at the 60 and the 120 FPS limit, no game
mode and no effects, on a private profile. Runs the 2.1.1 release EXE (--exe
release, the baseline) or this tree's build (--exe build) in a hard-linked copy of
the 2.1.1 NVIDIA package; rates come from the per-second [capture-timing] lines.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/screen-capture-rate.py --exe build|release
"""
from pathlib import Path
import argparse, ctypes, hashlib, json, os, re, shutil, subprocess, time
from ctypes import wintypes

ap = argparse.ArgumentParser(); ap.add_argument('--exe', choices=['build', 'release'], required=True); args = ap.parse_args()
B = Path('E:/项目/Veyra'); T = 'claude-capture-fps-audio-20261027'; R = Path(__file__).resolve().parents[2]
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; P = B/'tests'/T/('profile-' + args.exe)
L = B/'logs'/T/('screen-rate-' + args.exe); TMP = B/'tmp'/T/'screen-rate'; OUT = B/'reports'/T
PROBE = B/'tmp'/T/'bench/wgc_rate_probe.exe'
DONOR = B/'test-packages/codex-release-2.1.1-final-20261027/Veyra-2.1.1-NVIDIA-win64-portable'
for p in [L, TMP, OUT]: p.mkdir(parents=True, exist_ok=True)
if P.exists(): shutil.rmtree(P)
P.mkdir(parents=True)


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def replace(p, data):
    # Unlink first: a hard link to the donor is never written through.
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


manifest = json.loads((DONOR/'package-manifest.json').read_text('utf8'))
if not A.exists():
    for e in manifest['files']:
        rel = e['path']
        if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts): continue
        p = A/rel; p.parent.mkdir(parents=True, exist_ok=True); os.link(DONOR/rel, p)
    shutil.copyfile(DONOR/'package-manifest.json', A/'package-manifest.json')
exe = D/'veyra_qml_ui.exe' if args.exe == 'build' else DONOR/'veyra_qml_ui.exe'
replace(A/'veyra_qml_ui.exe', exe.read_bytes())
for folder in ['qml', 'i18n']:
    src = R if args.exe == 'build' else DONOR
    for p in (src/folder).rglob('*'):
        if p.is_file(): replace(A/folder/p.relative_to(src/folder), p.read_bytes())
for e in manifest['files']:
    if Path(e['path']).suffix.lower() in ['.dll', '.onnx', '.tflite', '.hsaco', '.dxil']: assert sha(A/e['path']) == e['sha256'], e['path']
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)), 'utf8')

probe = subprocess.Popen([str(PROBE), 'source', 'free60', '50'], stdout=subprocess.PIPE, text=True)
user32 = ctypes.WinDLL('user32', use_last_error=True)
hwnd = None
for _ in range(200):
    time.sleep(0.02); w = user32.FindWindowExW(None, None, 'VeyraRateProbeSource', None)
    while w:
        pid = wintypes.DWORD(); user32.GetWindowThreadProcessId(w, ctypes.byref(pid))
        if pid.value == probe.pid: hwnd = w; break
        w = user32.FindWindowExW(None, w, 'VeyraRateProbeSource', None)
    if hwnd: break
assert hwnd, 'probe window not found'

env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
log_file = L/'engine.log'; console = L/'console.log'
for f in (log_file, console):
    if f.exists(): f.unlink()
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(log_file))
main_src = (R if args.exe == 'build' else DONOR)/'qml/Veyra/Main.qml'
main = main_src.read_text('utf8'); pos = main.rfind('}')
loader = ('\nLoader { source: ' + json.dumps((R/'scripts/acceptance/screen-capture-rate.qml').as_uri()) +
          '; onLoaded: item.targetId=' + json.dumps(str(hwnd)) + ' }\n')
replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
started = time.time()
try:
    with console.open('wb') as f:
        run = subprocess.run([str(A/'veyra_qml_ui.exe'), '--size', '960x540', '--data-dir', str(P), '--exit-after', '70000'],
                             cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=120)
finally:
    replace(A/'qml/Veyra/Main.qml', main_src.read_bytes())
    probe_out = probe.communicate(timeout=40)[0]
seconds = round(time.time() - started, 1)
lines = log_file.read_text('utf8', errors='replace').splitlines(); text = console.read_text('utf8', errors='replace')
stamp = lambda l: time.mktime(time.strptime(l[:19], '%Y-%m-%dT%H:%M:%S')) + float('0' + l[19:23])
phases = {}
for i, l in enumerate(lines):
    if m := re.search(r'\[screen-rate-test\] steady-(start|end) fps=(\d)', l): phases.setdefault(m.group(2), {})[m.group(1)] = i
rates = {}
for fps, span in phases.items():
    rows = [(stamp(l), int(m.group(1)), int(m.group(2))) for l in lines[span['start']:span['end']]
            if (m := re.search(r'\[capture-timing\] revision=\d+ received=(\d+) processed=(\d+)', l))]
    first, last = rows[0], rows[-1]; dt = last[0] - first[0]
    rates[{'2': 'limit60', '3': 'limit120'}[fps]] = dict(seconds=round(dt, 2), receivedFps=round((last[1] - first[1]) / dt, 2), processedFps=round((last[2] - first[2]) / dt, 2))
result = dict(exe=args.exe, exeSha256=sha(A/'veyra_qml_ui.exe'), exit=run.returncode, seconds=seconds, passed='SR_PASS' in text and 'SR_FAIL' not in text,
              probe=probe_out.strip(), rates=rates,
              minUpdateInterval=[l.split('] ', 2)[-1] for l in lines if 'minUpdateInterval' in l],
              screenOpened=[l.split('] ', 2)[-1] for l in lines if '[screen] opened' in l],
              qmlErrors=[l for l in text.splitlines() if any(s in l for s in ['ReferenceError:', 'TypeError:', 'SR_FAIL'])][:10], log=str(log_file))
(OUT/f'screen-rate-{args.exe}.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps({k: result[k] for k in ['exe', 'exit', 'seconds', 'passed', 'probe', 'rates', 'minUpdateInterval']}, ensure_ascii=False, indent=1))
