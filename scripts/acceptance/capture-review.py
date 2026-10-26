"""One capture-card round with every effect on (claude-capture-review-20261026).

The task build runs in a hard-linked copy of the 2.1.0 NVIDIA runtime base with this
tree's QML/i18n, on a private profile seeded read-only from the user's own
user-data (capture device/format/audio and effect chain); screenshots go to the
task folder. capture-review.qml drives it: 60 s steady state, the 信号 card and
刷新显示器状态, then anti-aliasing screenshots on a paused game.
The user's own player must be closed first (the capture card is exclusive).
usage: py -3.11 -X utf8 -B -I scripts/acceptance/capture-review.py
"""
from pathlib import Path
import hashlib, json, os, re, shutil, subprocess, time

B = Path('E:/项目/Veyra'); T = 'claude-capture-review-20261026'; R = Path(__file__).resolve().parents[2]
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; P = B/'tests'/T/'profile'
L = B/'logs'/T/'capture'; TMP = B/'tmp'/T/'capture'; OUT = B/'reports'/T; SHOTS = B/'tests'/T/'shots'
USER_DATA = Path('E:/App/Veyra-2.1.0-test-NVIDIA-win64-portable/runtime_local/user-data-2.0.0')
for p in [L, TMP, OUT]: p.mkdir(parents=True, exist_ok=True)
for p in [P, SHOTS]:
    if p.exists(): shutil.rmtree(p)
    p.mkdir(parents=True)


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def replace(p, data):
    # Unlink first: a hard link to the donor is never written through.
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


running = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()), 'another player is running; the capture card is exclusive'

donor = B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA'
manifest = json.loads((donor/'package-manifest.json').read_text('utf8'))
if not A.exists():
    for e in manifest['files']:
        rel = e['path']
        if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts): continue
        p = A/rel; p.parent.mkdir(parents=True, exist_ok=True); os.link(donor/rel, p)
    shutil.copyfile(donor/'package-manifest.json', A/'package-manifest.json')
replace(A/'veyra_qml_ui.exe', (D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml', 'i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file(): replace(A/folder/p.relative_to(R/folder), p.read_bytes())
for e in manifest['files']:
    if Path(e['path']).suffix.lower() in ['.dll', '.onnx', '.tflite', '.hsaco', '.dxil']: assert sha(A/e['path']) == e['sha256'], e['path']

# The user's capture choice and effect chain, copied (never modified at the source).
seeded = {}
for f in USER_DATA.iterdir():
    if f.is_file(): shutil.copyfile(f, P/f.name); seeded[f.name] = sha(f)
prefs = json.loads((P/'qml-preferences.v1.json').read_text('utf8'))
prefs.update(screenshotDir=str(SHOTS), autoResume=False, prewarmEnhancement=False)
(P/'qml-preferences.v1.json').write_text(json.dumps(prefs, ensure_ascii=False), 'utf8')

env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
log_file = L/'engine.log'; console = L/'console.log'
for f in (log_file, console):
    if f.exists(): f.unlink()
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(log_file))
main = (R/'qml/Veyra/Main.qml').read_text('utf8'); pos = main.rfind('}')
loader = ('\nLoader { anchors.fill: parent; z: 1000; source: ' + json.dumps((R/'scripts/acceptance/capture-review.qml').as_uri()) +
          '; onLoaded: item.shotDir=' + json.dumps(SHOTS.as_posix()) + ' }\n')
replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
started = time.time()
try:
    with console.open('wb') as f:
        p = subprocess.run([str(A/'veyra_qml_ui.exe'), '--page', 'pro', '--size', '2400x1300', '--data-dir', str(P), '--exit-after', '280000'],
                           cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=300)
finally:
    replace(A/'qml/Veyra/Main.qml', (R/'qml/Veyra/Main.qml').read_bytes())
seconds = round(time.time() - started, 1)
log = log_file.read_text('utf8', errors='replace'); text = console.read_text('utf8', errors='replace')
lines = log.splitlines()
marks = [(i, m.group(1)) for i, l in enumerate(lines) if (m := re.search(r'\[capture-review\] (.+)', l))]
mark_at = lambda prefix: next((i for i, t in marks if t.startswith(prefix)), None)
s0, s1 = mark_at('steady-start'), mark_at('steady-end')
steady = lines[s0:s1] if s0 is not None and s1 is not None else []
count = lambda rows, tag: sum(1 for l in rows if tag in l)
def last(rows, tag):
    found = [l for l in rows if tag in l]
    return found[-1].split('] ', 2)[-1] if found else None
users = USER_DATA
untouched = all(sha(users/name) == digest for name, digest in seeded.items())
result = dict(
    exit=p.returncode, seconds=seconds, passed='CR_PASS' in text and 'CR_FAIL' not in text, userDataUntouched=untouched,
    marks=[t for _, t in marks],
    steady=dict(lines=len(steady), displayQueries=count(steady, '[display-query] query '), hdrPolls=count(steady, 'hdr-query-stall'),
                audioSampleWarnings=count(steady, '[WARN ] [capture-audio-samples]'), audioSampleSummaries=count(steady, '[INFO ] [capture-audio-samples]'),
                captureTiming=last(steady, '[capture-timing]'), frameFlow=last(steady, '[frame-flow]'), frameRate=last(steady, '[frame-rate]'),
                engineStalls=count(steady, '[engine-stall]'), errors=[l for l in steady if '[ERROR]' in l][:10]),
    displayQueriesWholeRun=[l.split('] ', 2)[-1] for l in lines if '[display-query]' in l][:40],
    antiAlias=[l.split('] ', 2)[-1] for l in lines if '[anti-alias]' in l or ('[node-dispatch]' in l and 'antialias' in l.lower())][:40],
    plans=[l.split('] ', 2)[-1] for l in lines if 'fixed execution plan accepted' in l][:20],
    qmlErrors=[l for l in text.splitlines() if any(s in l for s in ['ReferenceError:', 'TypeError:', 'Binding loop detected', 'CR_FAIL'])][:10],
    shots=sorted(str(f) for f in SHOTS.iterdir()), exeSha256=sha(A/'veyra_qml_ui.exe'), log=str(log_file))
(OUT/'capture-round.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps({k: result[k] for k in ['exit', 'seconds', 'passed', 'userDataUntouched', 'marks', 'steady']}, ensure_ascii=False, indent=1))
