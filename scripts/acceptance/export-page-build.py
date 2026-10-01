"""Task-specific build, with read-only dependency reuse and bounded subprocesses."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
BUILD = BASE / 'build/export-page-20261002'
TMP = BASE / 'tmp/export-page-20261002/build'
LOGS = BASE / 'logs/export-page-20261002'
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/export-page-control.py')], check=True)
for path in (BUILD, TMP, LOGS):
    path.mkdir(parents=True, exist_ok=True)
cache = BASE / 'build/field-issues-20260930-clean/CMakeCache.txt'
settings = []
for line in cache.read_text(encoding='utf-8').splitlines():
    if ':' not in line or '=' not in line:
        continue
    keytype, value = line.split('=', 1)
    key, kind = keytype.split(':', 1)
    if kind in ('INTERNAL', 'STATIC'):
        continue
    if (key.startswith('VEYRA_') and (key.endswith('_ROOT') or key.endswith('_DIR'))) or key in ('CMAKE_PREFIX_PATH', 'CMAKE_MAKE_PROGRAM', 'PROTOC', 'PKG_CONFIG_EXECUTABLE'):
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Read-only dependency location" FORCE)')
settings += ['set(VEYRA_BUILD_QML_UI ON CACHE BOOL "QML" FORCE)',
             'set(VEYRA_ENABLE_REMOTEPLAY ON CACHE BOOL "Preserve product configuration" FORCE)',
             'set(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR ON CACHE BOOL "Existing runtime API" FORCE)']
(TMP / 'dependencies.cmake').write_text('\n'.join(settings), encoding='utf-8')
targets = sys.argv[2:] or ['veyra_qml_ui', 'veyra_qml_quick_tests', 'veyra_qml_data_tests', 'veyra_qml_easing_tests', 'veyra_export_workflow_tests']
if any(not all(c.isalnum() or c == '_' for c in t) for t in targets):
    raise SystemExit('Invalid target')
cmd = TMP / 'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%EXPORT_CMAKE%" --preset x64-release -S "%EXPORT_ROOT%" -B "%EXPORT_BUILD%" -C "%EXPORT_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%EXPORT_CMAKE%" --build "%EXPORT_BUILD%" --parallel 4 --target ' + ' '.join(targets) + '\nexit /b %errorlevel%\n', encoding='ascii')
env = os.environ.copy()
env.update(VSLANG='1033', EXPORT_ROOT=str(ROOT), EXPORT_BUILD=str(BUILD), EXPORT_TMP=str(TMP), TEMP=str(TMP), TMP=str(TMP), EXPORT_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
log = LOGS / (sys.argv[1] if len(sys.argv) > 1 else 'build-001.log')
if log.exists():
    raise SystemExit('Refusing to overwrite build evidence')
with log.open('w', encoding='utf-8') as out:
    try:
        result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=900).returncode
    except subprocess.TimeoutExpired:
        result = 124
print('BUILD', result, log)
print('\n'.join(log.read_text(encoding='utf-8', errors='replace').splitlines()[-32:]))
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/export-page-control.py')], check=True)
raise SystemExit(result)
