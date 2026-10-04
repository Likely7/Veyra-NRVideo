"""Own-branch checkpoints and immutable scope receipts for the NR performance task."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
START = '8cdc612120cbf23ba116a33c3cb0a53e2043f718'
BRANCH = 'codex/perf-nr-20261004'
ARCHIVE = BASE / 'archives' / TASK
DESKTOP = Path('C:/Users/123/Desktop/Veyra DLSS Video Player')
ALLOWED = ('docs/', 'scripts/perf/', 'tests/perf/', 'tests/integration/', 'tests/unit/',
           'tests/qml/', 'src/engine/', 'include/veyra/engine/', 'src/pipeline/',
           'include/veyra/pipeline/', 'src/ngx/', 'include/veyra/ngx/', 'src/gfx/',
           'include/veyra/gfx/', 'src/diagnostics/', 'include/veyra/diagnostics/',
           'src/ui/', 'include/veyra/ui/', 'qml/Veyra/', 'shaders/', 'i18n/')
SINGLES = {'AGENTS.md', 'README.md', 'README_CN.md', 'VEYRA_PRODUCT_SPEC_V1.md',
           'THIRD_PARTY_NOTICES.md', 'CMakeLists.txt', 'apps/veyra-qml/main.cpp'}
FORBIDDEN = {'.dll', '.exe', '.lib', '.pdb', '.zip', '.7z', '.onnx', '.hsaco',
             '.ptx', '.cubin', '.f16', '.mp4', '.mkv', '.avi', '.dmp'}

def git(*args, root=ROOT, binary=False):
    p = subprocess.run(['git', '-C', str(root), *args], capture_output=True, check=True)
    return p.stdout if binary else p.stdout.decode('utf-8', errors='replace').replace('\r\n', '\n').strip()

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as f:
        json.dump(value, f, ensure_ascii=False, indent=2)

def guard():
    baseline = json.loads((ARCHIVE / 'start.json').read_text(encoding='utf-8'))
    assert ROOT.resolve() == Path(baseline['root']).resolve(), 'unexpected worktree'
    assert git('branch', '--show-current') == BRANCH, 'unexpected branch'
    assert git('rev-parse', 'main') == baseline['main'], 'main changed'
    assert git('status', '--porcelain=v1', '--untracked-files=normal', root=DESKTOP) == baseline['desktopStatus'], 'desktop status changed'
    paths = set(git('diff', '--name-only', START).splitlines())
    paths.update(git('ls-files', '--others', '--exclude-standard').splitlines())
    for p in paths:
        assert p in SINGLES or p.startswith(ALLOWED), 'out-of-scope path: ' + p
        assert Path(p).suffix.lower() not in FORBIDDEN, 'binary/source separation: ' + p
    git('diff', '--check')
    print(json.dumps({'scope': 'pass', 'head': git('rev-parse', 'HEAD'), 'paths': len(paths)}))

def init():
    assert git('branch', '--show-current') == BRANCH
    assert git('rev-parse', 'HEAD') == START
    for category in ('archives', 'build', 'tests', 'logs', 'tmp', 'test-packages'):
        (BASE / category / TASK).mkdir(parents=True, exist_ok=True)
    original = BASE / 'worktrees/rtss-compat-20261003/docs/PERF_PLAN_NR_2026-10-03.md'
    stored = ARCHIVE / 'PERF_PLAN_NR_2026-10-03.original.md'
    stored.write_bytes(original.read_bytes())
    assert sha(stored) == sha(ROOT / 'docs/PERF_PLAN_NR_2026-10-03.md')
    baseline = {'root': str(ROOT), 'branch': BRANCH, 'sourceBefore': START,
                'main': git('rev-parse', 'main'),
                'desktopStatus': git('status', '--porcelain=v1', '--untracked-files=normal', root=DESKTOP),
                'planOriginal': str(original), 'planSha256': sha(stored),
                'utc': datetime.now(timezone.utc).isoformat(),
                'worktrees': git('worktree', 'list', '--porcelain')}
    save(ARCHIVE / 'start.json', baseline)
    git('tag', 'checkpoint/pre-perf-nr-20261004', START)
    guard()
    print('INIT', ARCHIVE)

def checkpoint(node, result):
    guard()
    assert not git('status', '--porcelain=v1'), 'commit evidence before checkpoint'
    assert node.replace('-', '').isalnum()
    assert result in ('before', 'candidate', 'accepted', 'rejected', 'docs', 'baseline')
    tag = f'checkpoint/perf-nr-{node}-{result}-20261004'
    git('tag', tag)
    folder = ARCHIVE / f'{node}-{result}'
    folder.mkdir(exist_ok=False)
    head = git('rev-parse', 'HEAD')
    prior_file = ARCHIVE / 'latest.json'
    prior = json.loads(prior_file.read_text(encoding='utf-8')) if prior_file.exists() else None
    receipt = {'node': node, 'result': result, 'tag': tag, 'commit': head,
               'sourceBefore': START, 'parentCheckpoint': prior,
               'utc': datetime.now(timezone.utc).isoformat()}
    if prior is None or prior['commit'] != head:
        bundle = folder / 'source.bundle'
        args = ['bundle', 'create', str(bundle), tag]
        if prior is not None:
            args.append('^' + prior['commit'])
        git(*args)
        git('bundle', 'verify', str(bundle))
        receipt.update(bundle=str(bundle), bundleSha256=sha(bundle))
    patch = folder / 'source.patch'
    patch.write_bytes(git('diff', '--binary', START, head, binary=True))
    receipt['patchSha256'] = sha(patch)
    save(folder / 'checkpoint.json', receipt)
    prior_file.write_text(json.dumps({'commit': head, 'receipt': str(folder / 'checkpoint.json'),
                                     'tag': tag}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(receipt, ensure_ascii=False))

if __name__ == '__main__':
    if sys.argv[1:] == ['init']:
        init()
    elif sys.argv[1:] == ['guard']:
        guard()
    elif len(sys.argv) == 4 and sys.argv[1] == 'checkpoint':
        checkpoint(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit('init | guard | checkpoint <node> <before|candidate|accepted|rejected|docs|baseline>')
