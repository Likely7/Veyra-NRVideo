"""Fresh/incremental production builds with the audited 2.0.3 dependencies."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
variant, label = sys.argv[1:3]
assert variant.replace('-', '').isalnum() and label.replace('-', '').isalnum()
BUILD = BASE / 'build' / TASK / variant
TMP = BASE / 'tmp' / TASK / ('build-' + variant)
LOG = BASE / 'logs' / TASK / (label + '.log')
for p in (BUILD, TMP, LOG.parent):
    p.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/perf/nr-control.py'), 'guard'], check=True)
settings = []
cache = BASE / 'build/release-2.0.3-20261004/CMakeCache.txt'
for line in cache.read_text(encoding='utf-8').splitlines():
    if ':' not in line or '=' not in line or line.startswith(('#', '//')):
        continue
    keytype, value = line.split('=', 1)
    key, kind = keytype.split(':', 1)
    if kind in ('INTERNAL', 'STATIC') or key == 'VEYRA_DISPLAY_VERSION':
        continue
    if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH', 'CMAKE_MAKE_PROGRAM', 'PROTOC', 'PKG_CONFIG_EXECUTABLE'):
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Audited release dependency" FORCE)')
settings.append(f'set(VEYRA_DISPLAY_VERSION [[2.0.3-perf-{variant}]] CACHE STRING "Local NR performance build" FORCE)')
(TMP / 'dependencies.cmake').write_text('\n'.join(settings), encoding='utf-8')
targets = sys.argv[3:] or ['veyra_qml_ui']
assert all(t.replace('_', '').isalnum() for t in targets)
cmd = TMP / 'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 6 --target ' + ' '.join(targets) + '\nexit /b %errorlevel%\n', encoding='ascii')
env = os.environ.copy()
env.update(TEMP=str(TMP), TMP=str(TMP), VSLANG='1033', TASK_ROOT=str(ROOT), TASK_BUILD=str(BUILD),
           TASK_TMP=str(TMP), TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
with LOG.open('x', encoding='utf-8') as out:
    try:
        rc = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=ROOT, env=env,
                            stdout=out, stderr=subprocess.STDOUT, timeout=890).returncode
    except subprocess.TimeoutExpired:
        rc = 124
print('BUILD', rc, LOG)
print('\n'.join(LOG.read_text(encoding='utf-8', errors='replace').splitlines()[-20:]))
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/perf/nr-control.py'), 'guard'], check=True)
raise SystemExit(rc)
