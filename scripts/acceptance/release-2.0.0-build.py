"""Bounded build of the isolated release candidate using existing dependencies."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'release-2.0.0-20261002'
BUILD, TMP, LOGS = (BASE / part / TASK for part in ('build', 'tmp', 'logs'))
CONTROL = ROOT / 'scripts/acceptance/release-2.0.0-control.py'
subprocess.run([sys.executable, '-B', str(CONTROL)], check=True)
for p in (BUILD, TMP, LOGS):
    p.mkdir(parents=True, exist_ok=True)
settings = []
for line in (BASE / 'build/main-merge-20261002/CMakeCache.txt').read_text(encoding='utf8').splitlines():
    if ':' not in line or '=' not in line or line.startswith(('#', '//')):
        continue
    keytype, value = line.split('=', 1)
    key, kind = keytype.split(':', 1)
    if kind in ('INTERNAL', 'STATIC'):
        continue
    if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH', 'CMAKE_MAKE_PROGRAM', 'PROTOC', 'PKG_CONFIG_EXECUTABLE'):
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Preserve validated product configuration" FORCE)')
(TMP / 'dependencies.cmake').write_text('\n'.join(settings), encoding='utf8')
targets = sys.argv[2:] or ['veyra_qml_ui', 'veyra_qml_quick_tests', 'veyra_qml_data_tests',
                         'veyra_qml_easing_tests', 'veyra_ui_i18n_tests']
if any(not t.replace('_', '').isalnum() for t in targets):
    raise SystemExit('Invalid target')
cmd = TMP / 'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 4 --target ' + ' '.join(targets) + '\nexit /b %errorlevel%\n', encoding='ascii')
env = os.environ.copy()
env.update(VSLANG='1033', TASK_ROOT=str(ROOT), TASK_BUILD=str(BUILD), TASK_TMP=str(TMP),
           TEMP=str(TMP), TMP=str(TMP),
           TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
log = LOGS / (sys.argv[1] if len(sys.argv) > 1 else 'build-baseline.log')
with log.open('x', encoding='utf8') as stream:
    try:
        result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=ROOT, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=900).returncode
    except subprocess.TimeoutExpired:
        result = 124
print('BUILD', result, log)
print('\n'.join(log.read_text(encoding='utf8', errors='replace').splitlines()[-28:]))
subprocess.run([sys.executable, '-B', str(CONTROL)], check=True)
raise SystemExit(result)
