"""P0/P1-only guard and bounded command runner; never rewrites its baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def sha(path):
    return hashlib.file_digest(Path(path).open('rb'), 'sha256').hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE).decode('utf-8').strip()


def files(root):
    names = git(root, 'ls-files', '-c', '-o', '--exclude-standard', '-z').split(chr(0))
    return {n: sha(root / n) for n in sorted(set(names)) if n and (root / n).is_file()}


def differences(before, after, allowed=()):
    return [n for n in sorted(before.keys() | after.keys())
            if n not in allowed and before.get(n) != after.get(n)]


def identity_differences(expected, actual):
    return [key for key in expected if expected[key] != actual.get(key)]


def guard(manifest, manifest_sha):
    if sha(manifest) != manifest_sha.lower():
        raise RuntimeError('Immutable baseline SHA mismatch; do not regenerate it to pass.')
    b = json.loads(Path(manifest).read_text(encoding='utf-8'))
    root, original = Path(b['root']), Path(b['original_root'])
    actual = {
        'root': str(Path(git(root, 'rev-parse', '--show-toplevel')).resolve()),
        'branch': git(root, 'branch', '--show-current'),
        'head': git(root, 'rev-parse', 'HEAD'),
        'main': git(root, 'rev-parse', 'main'),
        'original_branch': git(original, 'branch', '--show-current'),
        'original_head': git(original, 'rev-parse', 'HEAD'),
    }
    errors = ['identity:' + x for x in identity_differences(b['identity'], actual)]
    errors += ['original:' + x for x in differences(b['original_files'], files(original))]
    errors += ['frozen:' + x for x in differences(b['files'], files(root), b['allow'])]
    if errors:
        raise RuntimeError('Scope guard FAIL: ' + json.dumps(errors, ensure_ascii=False))
    return b, {'status': 'pass', 'identity': actual, 'original_files_checked': len(b['original_files']),
               'isolated_files_checked': len(b['files']), 'mutable_paths': b['allow']}


def self_test():
    before = {'engine': 'a', 'test': 'b', 'guard': 'c'}
    cases = {
        'unchanged': not differences(before, dict(before)),
        'allowed_edit': not differences(before, {**before, 'test': 'd'}, ['test']),
        'frozen_edit_rejected': differences(before, {**before, 'engine': 'd'}, ['test']) == ['engine'],
        'deletion_rejected': differences(before, {'engine': 'a', 'test': 'b'}) == ['guard'],
        'new_file_rejected': differences(before, {**before, 'source/new': 'x'}) == ['source/new'],
        'original_even_allowed_rejected': differences(before, {**before, 'test': 'x'}) == ['test'],
        'branch_rejected': identity_differences({'branch': 'isolated'}, {'branch': 'main'}) == ['branch'],
        'main_rejected': identity_differences({'main': 'a'}, {'main': 'b'}) == ['main'],
        'wrong_root_rejected': identity_differences({'root': 'E'}, {'root': 'C'}) == ['root'],
    }
    print(json.dumps(cases, indent=2))
    return 0 if all(cases.values()) else 1


def run(args, b):
    limit = 900 if args.kind == 'build' else 300
    if not 0 < args.seconds <= limit:
        raise RuntimeError(f'{args.kind} timeout must be 1..{limit} seconds')
    output = Path(args.output).resolve()
    artifact_root = Path('E:/项目/Veyra').resolve()
    if not output.is_relative_to(artifact_root) or output.is_relative_to(Path(b['root'])):
        raise RuntimeError('Output must stay under E:/项目/Veyra and outside source workspace')
    output.mkdir(parents=True, exist_ok=True)
    ledger_path = output / 'commands.json'
    ledger = json.loads(ledger_path.read_text(encoding='utf-8')) if ledger_path.exists() else []
    attempts = [x for x in ledger if x['case'] == args.case and x['exit_code'] != 0]
    if len(attempts) >= 3:
        raise RuntimeError('Same-cause stop: initial diagnosis plus two failed attempts exhausted')
    if any(x['revision'] == args.revision for x in attempts):
        raise RuntimeError('No unchanged retry: provide a distinct evidence-backed revision or stop')
    command = list(args.command)
    if command and command[0] == '--':
        command.pop(0)
    if not command:
        raise RuntimeError('Missing child command')
    index = len(ledger) + 1
    prefix = output / f'{index:03d}-{args.case}'
    if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.case):
        raise RuntimeError('Unsafe case identifier')
    temp = artifact_root / 'tmp' / 'p0-p1-r53-20260927' / f'{index:03d}-{args.case}'
    temp.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'TEMP': str(temp), 'TMP': str(temp)}
    env['QT_PLUGIN_PATH'] = 'E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64/plugins'
    started = time.time()
    timed_out = False
    with Path(str(prefix) + '.stdout.log').open('wb') as out, Path(str(prefix) + '.stderr.log').open('wb') as err:
        child = subprocess.Popen(command, cwd=args.cwd or b['root'], env=env, stdout=out, stderr=err)
        try:
            code = child.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            # Terminate only the PID we created and its descendants, never by image name.
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], stdout=err, stderr=err, timeout=15)
            child.wait(timeout=15)
            code = 124
    record = {'case': args.case, 'revision': args.revision, 'command': command,
              'cwd': args.cwd or b['root'], 'pid': child.pid, 'timeout_seconds': args.seconds,
              'elapsed_seconds': round(time.time() - started, 3), 'exit_code': code,
              'timed_out': timed_out, 'started_epoch': started, 'log_prefix': str(prefix)}
    ledger.append(record)
    ledger_path.write_text(json.dumps(ledger, ensure_ascii=False, indent=2), encoding='utf-8')
    _, after = guard(args.baseline, args.sha256)
    record['post_guard'] = after['status']
    Path(str(prefix) + '.result.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return code


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['guard', 'self-test', 'run'])
    p.add_argument('--baseline')
    p.add_argument('--sha256')
    p.add_argument('--kind', choices=['test', 'build'], default='test')
    p.add_argument('--seconds', type=int, default=300)
    p.add_argument('--case')
    p.add_argument('--revision')
    p.add_argument('--output')
    p.add_argument('--cwd')
    args, command = p.parse_known_args()
    args.command = command
    if args.mode == 'self-test':
        return self_test()
    if not args.baseline or not args.sha256:
        p.error('--baseline and independently recorded --sha256 are required')
    b, result = guard(args.baseline, args.sha256)
    if args.mode == 'guard':
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    if not args.case or not args.revision or not args.output:
        p.error('run requires --case, --revision, --output')
    return run(args, b)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(2)
