"""Private refresh/VRR event-query check (claude-vrr-event-query-20261025).

Real 1080p30 file with VFG 2X in a hard-linked copy of the 2.1.0 NVIDIA runtime
base, the task build's EXE and this tree's QML/i18n, a private profile and log.
No capture device, never the user's package or profile. vrr-event-query.qml marks
each step in the engine log; this script counts DisplayStateService queries between
the marks. --case native uses the real NVAPI answer; --case novrr sets
VEYRA_TEST_VRR_LEVEL=1 (a monitor without VRR: no VRR presentation, no confirmation).

usage: py -3.11 -X utf8 -B -I scripts/acceptance/vrr-event-query.py --case native|novrr
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

B = Path('E:/项目/Veyra'); T = 'claude-vrr-event-query-20261025'; R = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(); parser.add_argument('--case', choices=['native', 'novrr'], required=True)
args = parser.parse_args()
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; P = B/'tests'/T/('profile-'+args.case)
L = B/'logs'/T/('native-'+args.case); TMP = B/'tmp'/T/('native-'+args.case); OUT = B/'reports'/T
for p in [L, TMP, OUT]: p.mkdir(parents=True, exist_ok=True)
if P.exists(): shutil.rmtree(P)  # fresh private profile per run
P.mkdir(parents=True)
for old in ['engine.log', 'console.log']:
    if (L/old).exists(): (L/old).unlink()


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def replace(p, data):
    # Unlink first: a hard link to the donor is never written through.
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


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
media = B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4'; assert media.is_file()
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)), 'utf8')
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           VEYRA_LOG_FILE=str(L/'engine.log'), QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1')
if args.case == 'novrr': env['VEYRA_TEST_VRR_LEVEL'] = '1'
main = (R/'qml/Veyra/Main.qml').read_text('utf8'); pos = main.rfind('}')
shot = B/'tests'/T/'shots'/f'display-tab-{args.case}.png'; shot.parent.mkdir(parents=True, exist_ok=True)
if shot.exists(): shot.unlink()
loader = ('\nLoader { source: ' + json.dumps(Path(__file__).with_suffix('.qml').as_uri()) +
          '; onLoaded: { item.mode=' + json.dumps(args.case) + '; item.shot=' + json.dumps(shot.as_posix()) +
          '; item.media=' + json.dumps(media.as_posix()) + ' } }\n')
replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
running = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()), 'another player is running; do not compete'
try:
    with (L/'console.log').open('wb') as f:
        p = subprocess.run([str(A/'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1280x800', '--data-dir', str(P), '--exit-after', '110000'],
                           cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=140)
finally:
    replace(A/'qml/Veyra/Main.qml', (R/'qml/Veyra/Main.qml').read_bytes())

text = (L/'console.log').read_text('utf8', errors='replace'); log = (L/'engine.log').read_text('utf8', errors='replace')
errors = [x for x in text.splitlines() if any(s in x for s in ['VRR_FAIL', '[ERROR]', 'ReferenceError:', 'TypeError:', 'Binding loop detected'])]
# The step marks and the queries DisplayStateService actually started, in log order.
events = []
for line in log.splitlines():
    if (m := re.search(r'\[vrr-test\] mark (\S+)', line)): events.append(('mark', m.group(1), line[:24]))
    elif (m := re.search(r'\[display-query\] query reason=(\S+)', line)): events.append(('query', m.group(1), line[:24]))
failures = [x for x in log.splitlines() if '[display-query]' in x and 'query failed' in x]
between, current = {}, 'before-idle-start'
for kind, name, stamp in events:
    if kind == 'mark': current = name; between.setdefault(current, [])
    else: between.setdefault(current, []).append(name)
# VRR presentation (vrrSync) after the switch decides whether confirmations follow.
vrr_sync = bool(re.search(r'\[pacing\] revision=\d+ .*variableRefresh=true .*vrrSync=true', log))
expected = {'before-idle-start': ['ui'], 'idle-start': [], 'open': ['session-start'], 'play-quiet': [], 'recheck': 1,
            'vrr-on': 2 if vrr_sync else 1, 'resize': ['vrr-confirm'] if vrr_sync else [], 'stopped': [], 'node-page': [], 'end': []}
problems = []
for step, want in expected.items():
    got = between.get(step)
    if got is None: problems.append(f'missing mark {step}'); continue
    if isinstance(want, int):
        if len(got) != want: problems.append(f'{step}: {len(got)} queries {got}, expected {want}')
    elif got != want: problems.append(f'{step}: queries {got}, expected {want}')
if vrr_sync and between.get('vrr-on', [])[-1:] != ['vrr-confirm']: problems.append('vrr-on: no VRR confirmation after the switch')
if args.case == 'novrr' and vrr_sync: problems.append('novrr: VRR presentation must not start')
passed = p.returncode == 0 and 'VRR_PASS' in text and not errors and not problems and not failures
result = dict(case=args.case, vrrPresentation=vrr_sync, exit=p.returncode, passed=passed, log=str(L), exeSha256=sha(A/'veyra_qml_ui.exe'), screenshot=str(shot) if shot.exists() else None,
              queriesBetweenMarks=between, problems=problems, queryFailures=failures, errors=errors,
              events=[list(e) for e in events],
              scope='real 1080p30 file, VFG 2X, private profile; counts background display queries per step; '
                    'native = real NVAPI answer of this monitor, novrr = simulated level 1; no capture device, no optical VRR measurement')
(OUT/f'native-{args.case}.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(dict(case=args.case, passed=passed, exit=p.returncode, problems=problems, errors=errors[:5], queries=between), ensure_ascii=False), flush=True)
assert passed, text[-4000:]
