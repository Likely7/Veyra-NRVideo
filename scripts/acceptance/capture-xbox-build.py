"""Build this isolated repair with the current libass/patched FFmpeg dependencies."""
from pathlib import Path
import os, subprocess, sys
ROOT=Path(__file__).resolve().parents[2]; BASE=Path('E:/项目/Veyra'); TASK='capture-xbox-field-20261005'
label=sys.argv[1];assert label.replace('-','').isalnum()
BUILD=BASE/'build'/TASK; TMP=BASE/'tmp'/TASK/'build'; LOG=BASE/'logs'/TASK/(label+'.log')
for p in (BUILD,TMP,LOG.parent):p.mkdir(parents=True,exist_ok=True)
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/capture-xbox-control.py')]
subprocess.run(guard,check=True)
settings=[];cache=BASE/'build/field-fixes-20261005/B/CMakeCache.txt'
for line in cache.read_text(encoding='utf8').splitlines():
    if ':' not in line or '=' not in line or line.startswith(('#','//')):continue
    keytype,value=line.split('=',1);key,kind=keytype.split(':',1)
    if kind in ('INTERNAL','STATIC') or key=='VEYRA_DISPLAY_VERSION':continue
    if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH','CMAKE_MAKE_PROGRAM','PROTOC','PKG_CONFIG_EXECUTABLE'):
        if key=='VEYRA_LIBASS_ROOT':assert (Path(value)/'include/ass/ass.h').is_file()
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Accepted dependencies" FORCE)')
assert any('VEYRA_LIBASS_ROOT' in l for l in settings)
settings.append('set(VEYRA_DISPLAY_VERSION [[2.0.3-streamfix1]] CACHE STRING "Local field repair" FORCE)')
(TMP/'dependencies.cmake').write_text('\n'.join(settings),encoding='utf8')
targets=sys.argv[2:] or ['veyra_qml_ui','veyra_xbox_tests','veyra_capture_compressed_tests','veyra_scene_tests','veyra_live_timing_tests']
assert all(t.replace('_','').isalnum() for t in targets)
cmd=TMP/'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 6 --target '+ ' '.join(targets)+'\nexit /b %errorlevel%\n',encoding='ascii')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),VSLANG='1033',TASK_ROOT=str(ROOT),TASK_BUILD=str(BUILD),TASK_TMP=str(TMP),TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
with LOG.open('x',encoding='utf8') as out:
    try:rc=subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=890).returncode
    except subprocess.TimeoutExpired:rc=124
print('BUILD',rc,LOG);print('\n'.join(LOG.read_text(encoding='utf8',errors='replace').splitlines()[-22:]))
subprocess.run(guard,check=True)
raise SystemExit(rc)
