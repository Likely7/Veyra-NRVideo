"""Read-only scope guard for the 2026-09-30 field fixes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path('E:/项目/Veyra/worktrees/p0-p1-r53-20260927')
ARCHIVE = Path('E:/项目/Veyra/archives/field-issues-20260930-start')
BASELINE_SHA = '9914a06b381fb6d5b90247d4169065b0c9503f84c48dd2272f571b771a18d354'
def guard():
    raw = (ARCHIVE / 'scope-baseline.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASELINE_SHA:
        raise RuntimeError('immutable field baseline changed')
    baseline = json.loads(raw)
    allow = set(baseline['allow'])
    # New batches list additional paths separately, never replacing start hashes.
    for amendment in sorted(ARCHIVE.glob('scope-amendment-*.json')):
        data = json.loads(amendment.read_text(encoding='utf-8-sig'))
        if data['baseline_sha256'] != BASELINE_SHA:
            raise RuntimeError('scope amendment has wrong parent')
        allow.update(data['allow'])
    # Windows checkout paths are case insensitive; git may report the existing
    # directory spelling (licenses) instead of a notice's spelling (LICENSES).
    allowed_paths = {name.casefold() for name in allow}
    names = set(filter(None, subprocess.check_output(['git', 'ls-files', '-c', '-o', '--exclude-standard', '-z'], cwd=ROOT).decode('utf8').split('\0')))
    problems = []
    for name in names | set(baseline['hashes']):
        if name.casefold() in allowed_paths:
            continue
        path = ROOT / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        if actual != baseline['hashes'].get(name):
            problems.append(name)
    for ref in ('HEAD', 'main'):
        actual = subprocess.check_output(['git', 'rev-parse', ref], cwd=ROOT, text=True).strip()
        if actual != baseline[ref.lower()]:
            problems.append('Git ' + ref)
    if subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip() != baseline['branch']:
        problems.append('Git branch')
    print('FIELD_SCOPE', 'FAIL' if problems else 'PASS', 'files', len(names), problems)
    return not problems
if __name__ == '__main__':
    sys.exit(0 if guard() else 3)
