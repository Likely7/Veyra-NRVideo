"""Preserve the integration inputs without touching any other checkout."""
from pathlib import Path
import subprocess, json, hashlib, zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = Path('E:/项目/Veyra/archives/capture-audio-main-20261025')
OUT.mkdir(parents=True, exist_ok=True)
def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])
records = []
for block in git(ROOT, 'worktree', 'list', '--porcelain').decode('utf-8').strip().split('\n\n'):
    fields = dict(line.split(' ', 1) for line in block.splitlines() if ' ' in line)
    path = Path(fields['worktree'])
    status = git(path, 'status', '--porcelain=v1', '-z')
    records.append(dict(fields, status=status.decode('utf-8')))
(OUT/'worktrees-before.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
source = Path('E:/项目/Veyra/worktrees/main-merge-20261002')
(OUT/'fullscreen.patch').write_bytes(git(source, 'diff', '--binary'))
files = ['AGENTS.md', 'qml/Veyra/Main.qml', 'qml/Veyra/NodePage.qml',
         'qml/Veyra/PlaylistWindow.qml', 'qml/Veyra/ProPage.qml',
         'docs/FULLSCREEN_EXIT_PLAN_2026-10-21.md', 'tests/qml/quick/tst_playlist_shortcuts.qml']
with zipfile.ZipFile(OUT/'fullscreen-dirty-input.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for name in files:
        z.write(source/name, name)
(OUT/'fullscreen-hashes.json').write_text(json.dumps({name: hashlib.sha256((source/name).read_bytes()).hexdigest() for name in files}, indent=2), encoding='utf-8')
(OUT/'refs-before.txt').write_bytes(git(ROOT, 'show-ref'))
print(json.dumps({'worktrees':len(records), 'dirty':sum(bool(x['status']) for x in records), 'archive':str(OUT)}, ensure_ascii=False))
