"""Local build for Claude's 2026-10-22 tasks.

Reuses the pinned CMake options of the accepted 2.1.0 main build (pr64 receipt),
points them at the given worktree and builds the requested targets into
E:/项目/Veyra/build/<task>. No downloads; dependencies are only read.

usage: py -3.11 -X utf8 -B -I build.py --task TASK --root WORKTREE [--label L] [--jobs N] [targets...]
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys, time

BASE = Path('E:/项目/Veyra')
RECEIPT = BASE / 'logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json'
CMAKE = r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
LIBEXE = r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/lib.exe'

parser = argparse.ArgumentParser()
parser.add_argument('--task', required=True)
parser.add_argument('--root', required=True)
parser.add_argument('--label', default='2.1.1-test')
parser.add_argument('--jobs', default='2')
parser.add_argument('--build-dir', help='Existing task-local build directory under E:/项目/Veyra/build')
parser.add_argument('--timeout', type=int, default=880)
parser.add_argument('--option', action='append', default=[], help='extra -D option NAME=VALUE')
parser.add_argument('targets', nargs='+')
args = parser.parse_args()
assert re.fullmatch(r'(?:claude|codex)-[a-z0-9.-]+', args.task), args.task
assert all(re.fullmatch(r'veyra(_[a-z0-9_]+)?', t) for t in args.targets), args.targets
ROOT = Path(args.root)
assert (ROOT / 'CMakeLists.txt').is_file(), ROOT
BUILD = Path(args.build_dir) if args.build_dir else BASE / 'build' / args.task
assert BUILD.resolve().is_relative_to((BASE/'build').resolve()), BUILD
TMP = BASE / 'tmp' / args.task / 'build'
LOGS = BASE / 'logs' / args.task / ('build-' + time.strftime('%Y%m%d-%H%M%S'))
for p in (BUILD, TMP, LOGS):
    p.mkdir(parents=True, exist_ok=True)


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def git(*a):
    return subprocess.check_output(['git', '-C', str(ROOT), *a], text=True, encoding='utf8').strip()


options = json.loads(RECEIPT.read_text('utf8'))['dependencies']
options.update(VEYRA_DISPLAY_VERSION=args.label,
               VEYRA_AMD_NR_TEST_GRAPH_SOURCE=str(ROOT / 'src/pipeline/EnhanceGraph.cpp'),
               Python3_EXECUTABLE=sys.executable)
for extra in args.option:
    key, value = extra.split('=', 1)
    options[key] = value
imports = BUILD / 'ffmpeg-imports'
imports.mkdir(exist_ok=True)

env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(TMP), TMP=str(TMP), VSLANG='1033', PYTHONDONTWRITEBYTECODE='1')
setup = subprocess.run(['cmd.exe', '/u', '/d', '/c', r'call C:\PROGRA~2\MICROS~3\2022\BUILDT~1\VC\Auxiliary\Build\vcvars64.bat >nul && set'],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding='utf-16-le', errors='replace', env=env, check=True)
for line in setup.stdout.splitlines():
    if '=' in line:
        k, v = line.split('=', 1)
        if k and not k.startswith('='):
            env[k] = v
env.update(TEMP=str(TMP), TMP=str(TMP), PYTHONDONTWRITEBYTECODE='1')

for short, dll in [('avformat', 'avformat-63'), ('avcodec', 'avcodec-63'), ('swresample', 'swresample-7'), ('swscale', 'swscale-10'), ('avutil', 'avutil-61')]:
    definition = Path(options['VEYRA_FFMPEG_ROOT']) / 'lib' / (dll + '.def')
    target = imports / (short + '.lib')
    if not target.exists():
        with (LOGS / ('import-' + short + '.log')).open('wb') as stream:
            subprocess.run([LIBEXE, '/nologo', '/machine:x64', '/def:' + str(definition), '/name:' + dll + '.dll', '/out:' + str(target)],
                           cwd=TMP, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=60)
    options['FFMPEG_lib' + short + '_LIBRARY'] = str(target)
    options['FFMPEG_lib' + short + '_LIBRARY_RELEASE'] = str(target)
    options['FFMPEG_lib' + short + '_LIBRARY_RELEASE_DIR'] = str(imports)

configure = [CMAKE, '-S', str(ROOT), '-B', str(BUILD), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'] + ['-D' + k + '=' + v for k, v in options.items()]
build = [CMAKE, '--build', str(BUILD), '--parallel', args.jobs, '--target', *args.targets]


def run(name, command, deadline):
    # UTF-8 console first: under code page 936 CMake silently drops rules.ninja (build-console-codepage).
    script = TMP / (name + '.cmd')
    script.write_text('@echo off\nchcp 65001 >nul\n' + subprocess.list2cmdline(command) + '\nexit /b %errorlevel%\n', encoding='utf8')
    with (LOGS / (name + '.log')).open('wb') as stream:
        try:
            rc = subprocess.run(['cmd.exe', '/d', '/c', str(script)], cwd=TMP, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=max(1, deadline - time.monotonic())).returncode
        except subprocess.TimeoutExpired:
            rc = 124
    print(name, rc, LOGS / (name + '.log'), flush=True)
    if rc:
        text = (LOGS / (name + '.log')).read_text('utf8', errors='replace')
        errors = [l for l in text.splitlines() if ' error ' in l or 'error C' in l or 'error LNK' in l or 'FAILED' in l]
        print('\n'.join(errors[:40]) if errors else text[-6000:], flush=True)
        raise SystemExit(rc)


start = time.monotonic()
deadline = start + args.timeout
if not (BUILD / 'build.ninja').exists():
    run('configure', configure, deadline)
    if not (BUILD / 'CMakeFiles/rules.ninja').exists():
        print('ERROR: rules.ninja missing after configure (console code page?)', flush=True)
        raise SystemExit(3)
run('compile', build, deadline)
receipt = dict(head=git('rev-parse', 'HEAD'), dirty=bool(git('status', '--porcelain')), label=args.label, targets=args.targets,
               seconds=round(time.monotonic() - start, 1),
               executables={t + '.exe': sha(BUILD / (t + '.exe')) for t in args.targets if (BUILD / (t + '.exe')).exists()})
(LOGS / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding='utf8')
print('BUILD PASS', receipt['seconds'], 'seconds', args.targets, flush=True)
