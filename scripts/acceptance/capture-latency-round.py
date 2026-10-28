"""One capture-card round for 60->30 phase and driver buffering (claude-capture-fps-audio-20261027).

1. dshow_latency_probe (tmp/<task>/bench) measures the driver's frame age for the user's
   current format with no / 1 / 3 suggested buffers and for a few other formats.
2. This tree's build captures for 30 s on a private profile seeded read-only from the
   user's user-data (device, format, 60->30, effect chain); the log shows the picture
   compare, the 60->30 phase and the driver age.
The user's own player must be closed first (the capture card is exclusive).
usage: py -3.11 -X utf8 -B -I scripts/acceptance/capture-latency-round.py [--app-only]
"""
from pathlib import Path
import hashlib, json, os, re, shutil, subprocess, time

B = Path('E:/项目/Veyra'); T = 'claude-capture-fps-audio-20261027'; R = Path(__file__).resolve().parents[2]
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; P = B/'tests'/T/'profile-capture'
L = B/'logs'/T/'capture-round'; TMP = B/'tmp'/T/'capture-round'; OUT = B/'reports'/T
PROBE = B/'tmp'/T/'bench/dshow_latency_probe.exe'
USER_DATA = Path('E:/App/Veyra-2.1.0-test-NVIDIA-win64-portable/runtime_local/user-data-2.0.0')
for p in [L, TMP, OUT]: p.mkdir(parents=True, exist_ok=True)
running = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, errors='replace')
assert not any(l.lower().startswith(('"veyra', '"obs64', '"obs32')) for l in running.splitlines()), 'another program may hold the capture card'
assert (A/'veyra_qml_ui.exe').exists(), 'run screen-capture-rate.py --exe build first (it stages the app)'


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def replace(p, data):
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


# 1. Driver buffering probe (--app-only skips it).
import sys
probe = []
for args in [] if '--app-only' in sys.argv else [('2560', '1440', '60', 'P010', '0'), ('2560', '1440', '60', 'P010', '1'), ('2560', '1440', '60', 'P010', '3'),
             ('2560', '1440', '60', 'NV12', '0'), ('1920', '1080', '60', 'NV12', '0'), ('2560', '1440', '120', 'NV12', '0'),
             ('2560', '1440', '60', 'MJPG', '0')]:
    out = subprocess.run([str(PROBE), 'run', 'T911', *args, '6'], capture_output=True, text=True, timeout=60)
    line = next((l for l in out.stdout.splitlines() if l.startswith(('RUN', 'FORMAT', 'CONNECT', 'DEVICE', 'PIN'))), out.stdout.strip()[-200:])
    probe.append(dict(args=args, exit=out.returncode, line=line)); print(line, flush=True)
    time.sleep(1)

# 2. The build on the user's capture setup (copied, never modified at the source).
replace(A/'veyra_qml_ui.exe', (D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml', 'i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file(): replace(A/folder/p.relative_to(R/folder), p.read_bytes())
if P.exists(): shutil.rmtree(P)
P.mkdir(parents=True)
seeded = {}
for f in USER_DATA.iterdir():
    if f.is_file(): shutil.copyfile(f, P/f.name); seeded[f.name] = sha(f)
prefs = json.loads((P/'qml-preferences.v1.json').read_text('utf8'))
prefs.update(autoResume=True, prewarmEnhancement=False, screenshotDir=str(TMP))
(P/'qml-preferences.v1.json').write_text(json.dumps(prefs, ensure_ascii=False), 'utf8')
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
log_file = L/'engine.log'; console = L/'console.log'
for f in (log_file, console):
    if f.exists(): f.unlink()
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(log_file))
with console.open('wb') as f:
    run = subprocess.run([str(A/'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1600x900', '--data-dir', str(P), '--exit-after', '40000'],
                         cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=90)
lines = log_file.read_text('utf8', errors='replace').splitlines()
grab = lambda tag: [l.split('] ', 2)[-1] for l in lines if tag in l]
result = dict(probe=probe, app=dict(exit=run.returncode, userDataUntouched=all(sha(USER_DATA/n) == d for n, d in seeded.items()),
              pictureCompare=grab('[capture-dedup] picture compare'), phase=grab('60->30 phase'), captureRate=grab('[capture-rate] revision')[:4],
              driverAge=grab('[capture-driver-age]'), buffer=grab('[capture-buffer]'), format=grab('[capture] selected')[:2] or grab('native ConnectDirect')[:2],
              frameRate=grab('[frame-rate]')[-3:], timing=[l[:400] for l in grab('[capture-timing]')[-2:]], log=str(log_file)))
(OUT/'capture-latency-round.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(result['app'], ensure_ascii=False, indent=1))
