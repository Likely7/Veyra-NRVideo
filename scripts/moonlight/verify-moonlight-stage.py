#!/usr/bin/env python3
"""Verify that the moonlight-common-c checkout is exactly the pinned one.

Used by cmake/VeyraMoonlight.cmake before anything is compiled: the commit of
the library and of both bundled submodules must match dependency-lock.json and
the tree must have no local modifications, so a build can never silently use a
different protocol implementation.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def git(directory: Path, *args: str) -> str:
    result = subprocess.run(['git', '-C', str(directory), *args], capture_output=True, text=True, encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError(f'git {" ".join(args)} failed in {directory}: {result.stderr.strip()}')
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, help='moonlight-common-c checkout')
    parser.add_argument('--lock', required=True, help='dependency-lock.json')
    args = parser.parse_args()
    lock = json.loads(Path(args.lock).read_text(encoding='utf-8'))['moonlight_common_c']
    source = Path(args.source)
    problems = []
    if not (source / 'src' / 'Limelight.h').is_file():
        print(f'MOONLIGHT_STAGE FAIL: {source} is not a moonlight-common-c checkout', file=sys.stderr)
        return 2
    head = git(source, 'rev-parse', 'HEAD')
    if head != lock['commit']:
        problems.append(f'library commit {head} != pinned {lock["commit"]}')
    if lock['patches'] == [] and git(source, 'status', '--porcelain', '--untracked-files=no'):
        problems.append('tracked files are modified but the lock lists no patches')
    for name, pin in lock['submodules'].items():
        sub = source / name
        if not (sub / 'LICENSE').is_file() and not (sub / 'LICENSE.txt').is_file():
            problems.append(f'submodule {name} is not checked out')
            continue
        commit = git(sub, 'rev-parse', 'HEAD')
        if commit != pin['commit']:
            problems.append(f'submodule {name} commit {commit} != pinned {pin["commit"]}')
    if problems:
        print('MOONLIGHT_STAGE FAIL: ' + '; '.join(problems), file=sys.stderr)
        return 1
    print(f'MOONLIGHT_STAGE PASS {head}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
