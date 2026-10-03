"""Scope for the authorized window fix; HDR PRs remain review-only."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = 'a927f92c6a35522a6081b61b00fc37a35bd5d988'
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode('utf-8').strip()
assert git('branch', '--show-current') == 'codex/minimal-edge-hdr-review-20261004'
assert git('rev-parse', 'checkpoint/pre-minimal-edge-hdr-review-20261004^{commit}') == BASE
changed = set(git('diff', '--name-only', BASE).splitlines()) | set(git('ls-files', '--others', '--exclude-standard').splitlines())
extra = {p for p in changed if p not in {'AGENTS.md', 'apps/veyra-qml/main.cpp', 'include/veyra/engine/PresentationGeometry.h', 'src/engine/VideoPresenter.cpp'} and not p.startswith(('docs/', 'scripts/acceptance/minimal-edge-', 'scripts/acceptance/hdr-pr-'))}
binary = {p for p in changed if Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.zip','.whl','.f16','.f32','.i32','.bin','.onnx','.hsaco','.cso','.ptx','.safetensors','.addon64'}}
if extra or binary:
    raise SystemExit('MINIMAL EDGE SCOPE FAIL: ' + ', '.join(sorted(extra | binary)))
print('MINIMAL EDGE SCOPE PASS', len(changed), 'paths; HDR product code untouched')
