"""Fresh isolated build, using only the existing product dependency paths."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'release-2.0.3-20261004'
BUILD, TMP, LOGS = (BASE / part / TASK for part in ('build', 'tmp', 'logs'))
CONTROL = ROOT / 'scripts/acceptance/release-2.0.3-control.py'
subprocess.run([sys.executable, '-B', str(CONTROL)], check=True)
for p in (BUILD, TMP, LOGS):
    p.mkdir(parents=True, exist_ok=True)
settings = []
for line in (BASE / 'build/release-2.0.2-20261003/CMakeCache.txt').read_text(encoding='utf8').splitlines():
    if ':' not in line or '=' not in line or line.startswith(('#', '//')):
        continue
    keytype, value = line.split('=', 1)
    key, kind = keytype.split(':', 1)
    if kind in ('INTERNAL', 'STATIC'):
        continue
    if key == 'VEYRA_DISPLAY_VERSION': continue
    if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH', 'CMAKE_MAKE_PROGRAM', 'PROTOC', 'PKG_CONFIG_EXECUTABLE'):
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Existing dependency" FORCE)')
settings.append('set(VEYRA_CUDA_DRIVER_INCLUDE_DIR [[E:/项目/Veyra/deps/nvidia-vfx-audit-20260921/nvidia/cuda_runtime/include]] CACHE PATH "External CUDA driver headers" FORCE)')
(TMP / 'dependencies.cmake').write_text('\n'.join(settings), encoding='utf8')
targets = sys.argv[2:] or ['veyra_qml_ui', 'veyra_vfg_gpu_tests', 'veyra_vfg_export_probe', 'veyra_vfg_settings_tests']
if any(not t.replace('_', '').isalnum() for t in targets):
    raise SystemExit('Invalid target')
cmd = TMP / 'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 6 --target ' + ' '.join(targets) + '\nexit /b %errorlevel%\n', encoding='ascii')
env = os.environ.copy()
env.update(VSLANG='1033', TASK_ROOT=str(ROOT), TASK_BUILD=str(BUILD), TASK_TMP=str(TMP),
           TEMP=str(TMP), TMP=str(TMP),
           TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
log = LOGS / sys.argv[1]
with log.open('x', encoding='utf8') as output:
    try:
        code = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=ROOT, env=env,
                              stdout=output, stderr=subprocess.STDOUT, timeout=890).returncode
    except subprocess.TimeoutExpired:
        code = 124
print('BUILD', code, log)
print('\n'.join(log.read_text(encoding='utf8', errors='replace').splitlines()[-25:]))
subprocess.run([sys.executable, '-B', str(CONTROL)], check=True)
raise SystemExit(code)
