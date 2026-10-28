"""Three-step SR rows in the real app (claude-sr-stage-20261028).

Runs this tree's build in the task's staged copy (tmp stage_app.py: hard links to the
2.1.1 portable donor, this build's EXE / shaders / QML), on a private profile, playing a
4K file with RTX Video SR while sr-stage-ui.qml drives the list SR card. Main.qml gets the
test Loader only inside that copy.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/sr-stage-ui.py [media]
"""
from pathlib import Path
import argparse, json, os, shutil, subprocess, sys

parser=argparse.ArgumentParser()
parser.add_argument('media',nargs='?',default='E:/项目/Veyra/tests/nr-manual-scale-20261024/media/nr-4k30.mp4')
parser.add_argument('--task',default='claude-sr-stage-20261028')
args=parser.parse_args()
assert args.task.startswith(('codex-','claude-')) and '/' not in args.task and '\\' not in args.task and '..' not in args.task
B = Path('E:/项目/Veyra'); T = args.task; R = Path(__file__).resolve().parents[2]
A = B/'tests/claude-sr-stage-20261028/app'; P = B/'tests'/T/'profile-ui'; L = B/'logs'/T/'ui'; TMP = B/'tmp'/T/'ui'; OUT = B/'reports'/T
MEDIA = Path(args.media)
assert (A/'veyra_qml_ui.exe').exists(), 'run the task stage_app.py first'
for p in [L, TMP, OUT]: p.mkdir(parents=True, exist_ok=True)


def replace(p, data):
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()  # never write through a hard link
    p.write_bytes(data)


if P.exists(): shutil.rmtree(P)
P.mkdir(parents=True)
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)), 'utf8')
main_src = R/'qml/Veyra/Main.qml'; main = main_src.read_text('utf8'); pos = main.rfind('}')
loader = ('\nLoader { anchors.fill: parent; source: ' + json.dumps((R/'scripts/acceptance/sr-stage-ui.qml').as_uri()) +
          '; onLoaded: item.media=' + json.dumps(MEDIA.as_posix()) + ' }\n')
replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
log_file = L/'engine.log'; console = L/'console.log'
for f in (log_file, console):
    if f.exists(): f.unlink()
env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'), CUDA_CACHE_PATH=str(TMP/'cuda'),
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(log_file))
try:
    with console.open('wb') as f:
        run = subprocess.run([str(A/'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1600x900', '--data-dir', str(P), '--exit-after', '200000'],
                             cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=240)
finally:
    replace(A/'qml/Veyra/Main.qml', main_src.read_bytes())
text = console.read_text('utf8', errors='replace')
lines = log_file.read_text('utf8', errors='replace').splitlines() if log_file.exists() else []
grab = lambda tag: [l.split('] ', 2)[-1] for l in lines if tag in l]
result = dict(exit=run.returncode, media=str(MEDIA), passed='SR_STAGE_UI_PASS' in text and 'SR_STAGE_UI_FAIL' not in text,
              steps=grab('[sr-stage-test]'), videoSr=grab('[video-sr] selected'), resolution=grab('[resolution] source=')[:12],
              accepted=grab('accepted srTarget=')[:12],
              qmlErrors=[l for l in text.splitlines() if any(s in l for s in ['ReferenceError:', 'TypeError:', 'Binding loop detected', 'is not a type', 'ambiguous'])][:8],
              log=str(log_file))
(OUT/'sr-stage-ui.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(result, ensure_ascii=False, indent=1))
sys.exit(0 if result['passed'] and not result['qmlErrors'] else 1)
