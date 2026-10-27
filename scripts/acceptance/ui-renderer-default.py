"""设置 → 界面绘制 across real launches (claude-software-ui-default-20261026).

Four launches of the task build on one fresh private profile, in a hard-linked copy of
the 2.1.0 NVIDIA runtime base with this tree's QML/i18n; no media, capture or user
package. ui-renderer-default.qml picks values through the real 设置 control.
  1 fresh profile          -> software by default; picking GPU asks to restart
  2 preference GPU         -> D3D12 interface; picking 软件 asks to restart (GPU kept)
  3 GPU + --overlay-compat-restart -> software (compatibility); picking 软件 needs no restart
  4 preference software    -> software by default
usage: py -3.11 -X utf8 -B -I scripts/acceptance/ui-renderer-default.py
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--task',default='claude-software-ui-default-20261026')
parser.add_argument('--package-dir',type=Path)
options=parser.parse_args()
assert re.fullmatch(r'(claude|codex)-[a-z0-9.-]+',options.task)
B = Path('E:/项目/Veyra'); T = options.task; R = Path(__file__).resolve().parents[2]
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; P = B/'tests'/T/'profile'
L = B/'logs'/T/'launches'; TMP = B/'tmp'/T/'launches'; OUT = B/'reports'/T; SHOTS = B/'tests'/T/'shots'
for p in [L, TMP, OUT, SHOTS]: p.mkdir(parents=True, exist_ok=True)
P.mkdir(parents=True,exist_ok=False)


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def replace(p, data):
    # Unlink first: a hard link to the donor is never written through.
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


donor = options.package_dir or B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA'
manifest = json.loads((donor/'package-manifest.json').read_text('utf8'))
if not A.exists():
    for e in manifest['files']:
        rel = e['path']
        if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts): continue
        p = A/rel; p.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(donor/rel, p)
    shutil.copyfile(donor/'package-manifest.json', A/'package-manifest.json')
replace(A/'veyra_qml_ui.exe', (D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml', 'i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file(): replace(A/folder/p.relative_to(R/folder), p.read_bytes())
for e in manifest['files']:
    if Path(e['path']).suffix.lower() in ['.dll', '.onnx', '.tflite', '.hsaco', '.dxil']: assert sha(A/e['path']) == e['sha256'], e['path']
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)), 'utf8')
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1')
main = (R/'qml/Veyra/Main.qml').read_text('utf8'); pos = main.rfind('}')
expect = {
    1: dict(app='software UI by default', renderer='software', state='softwareUiActive=true overlayCompatActive=false pref= segment=software',
            picked='picked=gpu pref=gpu confirmShown=true', done='done pref=gpu'),
    2: dict(app='GPU UI: interface drawing preference: GPU', renderer='d3d12', state='softwareUiActive=false overlayCompatActive=false pref=gpu segment=gpu',
            picked='picked=software pref=software confirmShown=true', done='done pref=gpu'),
    3: dict(app='RTSS compatibility: software UI', renderer='software', state='softwareUiActive=true overlayCompatActive=true pref=gpu segment=gpu',
            picked='picked=software pref=software confirmShown=false', done='done pref=software'),
    4: dict(app='software UI by default', renderer='software', state='softwareUiActive=true overlayCompatActive=false pref=software segment=software',
            picked=None, done='done pref=software'),
}
running = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()), 'another player is running; do not compete'
results = []
try:
    for step, want in expect.items():
        log_file = L/f'engine-{step}.log'; console = L/f'console-{step}.log'
        for f in (log_file, console):
            if f.exists(): f.unlink()
        shot = SHOTS/f'settings-compat-{step}.png'
        if shot.exists(): shot.unlink()
        loader = ('\nLoader { source: ' + json.dumps((R/'scripts/acceptance/ui-renderer-default.qml').as_uri()) +
                  '; onLoaded: { item.shot=' + json.dumps(shot.as_posix()) + '; item.step=' + str(step) + ' } }\n')
        replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
        args = [str(A/'veyra_qml_ui.exe'), '--page', 'home', '--size', '1280x800', '--data-dir', str(P), '--exit-after', '40000']
        if step == 3: args.append('--overlay-compat-restart')
        with console.open('wb') as f:
            p = subprocess.run(args, cwd=A, env={**env, 'VEYRA_LOG_FILE': str(log_file)}, stdout=f, stderr=subprocess.STDOUT, timeout=70)
        log = log_file.read_text('utf8', errors='replace'); text = console.read_text('utf8', errors='replace')
        renderer = 'd3d12' if 'interface renderer=d3d12' in log else ('d3d11' if 'interface renderer=d3d11' in log else ('software' if '[app] software UI:' in log else 'unknown'))
        lines = [m.group(1) for m in re.finditer(r'\[ui-renderer-test\] (.+)', log)]
        problems = []
        if p.returncode != 0: problems.append(f'exit {p.returncode}')
        if 'UIR_PASS' not in text or 'UIR_FAIL' in text: problems.append('driver did not pass')
        if f'[app] {want["app"]}' not in log: problems.append(f'missing app log: {want["app"]}')
        if renderer != want['renderer']: problems.append(f'renderer {renderer}, expected {want["renderer"]}')
        if not any(l.endswith(want['state']) for l in lines): problems.append(f'state line {want["state"]!r} missing: {lines}')
        if want['picked'] and want['picked'] not in lines: problems.append(f'pick line {want["picked"]!r} missing: {lines}')
        if want['done'] not in lines: problems.append(f'done line {want["done"]!r} missing: {lines}')
        if not any(l.startswith('sr-selection=') for l in lines): problems.append('SR default / saved-selection check missing')
        errors = [x for x in text.splitlines() if any(s in x for s in ['[ERROR]', 'ReferenceError:', 'TypeError:', 'Binding loop detected'])]
        if errors: problems.append('QML/engine errors')
        results.append(dict(step=step, renderer=renderer, passed=not problems, problems=problems, lines=lines, errors=errors[:10],
                            screenshot=str(shot) if shot.exists() else None, log=str(log_file)))
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)
finally:
    replace(A/'qml/Veyra/Main.qml', (R/'qml/Veyra/Main.qml').read_bytes())
passed = all(r['passed'] for r in results) and len(results) == 4
(OUT/'launches.json').write_text(json.dumps(dict(passed=passed, exeSha256=sha(A/'veyra_qml_ui.exe'), launches=results,
    scope='four real launches on one private profile; 设置 control picked through its own signal; renderer read from the app log'),
    ensure_ascii=False, indent=1), 'utf8')
assert passed, 'see ' + str(OUT/'launches.json')
