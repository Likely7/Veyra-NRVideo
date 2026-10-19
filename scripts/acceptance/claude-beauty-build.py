"""Configure (once) and build selected targets for the claude/beauty-20261017 branch.

Mirrors scripts/acceptance/test-2.0.8-build.py: dependency paths are taken from the most recent accepted
CMake cache, FFmpeg import libraries are generated from the pinned DEF files inside this build tree, and
nothing outside E:/项目/Veyra/{build,logs,tmp}/<task> is written. No downloads, no packaging.

usage (run from a console whose output code page is 65001, see memory build-console-codepage):
  chcp.com 65001 >/dev/null && py -3.11 -B scripts/acceptance/claude-beauty-build.py --target veyra_beauty_p0_tests
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, time

parser = argparse.ArgumentParser()
parser.add_argument('--task', default='claude-beauty-20261017')
parser.add_argument('--label', default='2.0.9-beauty')
parser.add_argument('--target', action='append', default=[])
parser.add_argument('--template-cache', default='E:/项目/Veyra/build/pr57-capture-window-20261017/CMakeCache.txt')
parser.add_argument('--reconfigure', action='store_true')
parser.add_argument('--parallel', default='8')
args = parser.parse_args()
assert re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,79}', args.task)
assert args.target, 'pass at least one --target'
assert all(re.fullmatch(r'veyra_[a-z0-9_]+', t) for t in args.target)
ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = args.task
BUILD = BASE / 'build' / TASK
TMP = BASE / 'tmp' / TASK / 'build'
LOG = BASE / 'logs' / TASK / ('build-' + time.strftime('%Y%m%d-%H%M%S'))
for p in (BUILD, TMP, LOG):
    p.mkdir(parents=True, exist_ok=True)


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


text = Path(args.template_cache).read_text('utf8')
options = {k: v for k, t, v in re.findall(r'^(VEYRA_[^:]+|CMAKE_MAKE_PROGRAM|CMAKE_PREFIX_PATH):([^=]+)=(.*)$', text, re.M)
           if t not in ('INTERNAL', 'STATIC')}
for k in ('PROTOC', 'PKG_CONFIG_EXECUTABLE', 'SDL3_DIR', 'LibDataChannel_DIR', 'Opus_DIR', 'nlohmann_json_DIR'):
    options[k] = re.search(r'^' + k + r':[^=]+=(.*)$', text, re.M).group(1)
options.update(VEYRA_DECKLINK_SDK_DIR=str(BASE / 'deps/decklink-sdk-obs31-20261009'),
               VEYRA_AMD_NR_TEST_GRAPH_SOURCE=str(ROOT / 'src/pipeline/EnhanceGraph.cpp'),
               VEYRA_FFMPEG_ROOT=str(BASE / 'deps/ffmpeg-filter-issues41-45-20261016'),
               VEYRA_DISPLAY_VERSION=args.label, Python3_EXECUTABLE=os.sys.executable)
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

importdir = BUILD / 'ffmpeg-imports'
importdir.mkdir(exist_ok=True)
lib = r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/lib.exe'
for short, dll in [('avformat', 'avformat-63'), ('avcodec', 'avcodec-63'), ('swresample', 'swresample-7'), ('swscale', 'swscale-10'), ('avutil', 'avutil-61')]:
    definition = Path(options['VEYRA_FFMPEG_ROOT']) / 'lib' / (dll + '.def')
    target = importdir / (short + '.lib')
    if not target.exists():
        with (LOG / ('import-' + short + '.log')).open('wb') as stream:
            subprocess.run([lib, '/nologo', '/machine:x64', '/def:' + str(definition), '/name:' + dll + '.dll', '/out:' + str(target)],
                           cwd=TMP, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=30)
    options['FFMPEG_lib' + short + '_LIBRARY'] = str(target)
    options['FFMPEG_lib' + short + '_LIBRARY_RELEASE'] = str(target)
    options['FFMPEG_lib' + short + '_LIBRARY_RELEASE_DIR'] = str(importdir)

cmake = r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
configure = [cmake, '-S', str(ROOT), '-B', str(BUILD), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'] + ['-D' + k + '=' + v for k, v in options.items()]
build = [cmake, '--build', str(BUILD), '--parallel', args.parallel, '--target', *args.target]
steps = []
if args.reconfigure or not (BUILD / 'build.ninja').exists():
    steps.append(('configure', configure))
steps.append(('compile', build))
start = time.monotonic()
results = []
for name, command in steps:
    with (LOG / (name + '.log')).open('wb') as stream:
        try:
            rc = subprocess.run(command, cwd=TMP, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=max(1, 890 - (time.monotonic() - start))).returncode
        except subprocess.TimeoutExpired:
            rc = 124
    results.append(dict(step=name, exit=rc, seconds=round(time.monotonic() - start, 1)))
    print(name, rc, LOG / (name + '.log'), flush=True)
    if rc:
        print((LOG / (name + '.log')).read_text('utf8', errors='replace')[-9000:], flush=True)
        (LOG / 'failure.json').write_text(json.dumps(dict(results=results, commands=[c for _, c in steps]), ensure_ascii=False, indent=2), 'utf8')
        raise SystemExit(rc)
exes = {t: sha(BUILD / (t + '.exe')) for t in args.target if (BUILD / (t + '.exe')).exists()}
(LOG / 'receipt.json').write_text(json.dumps(dict(passed=True, results=results, targets=args.target, executables=exes,
                                                  seconds=round(time.monotonic() - start, 1)), ensure_ascii=False, indent=2), 'utf8')
print('PASS', args.target, round(time.monotonic() - start, 1), 'seconds', flush=True)
