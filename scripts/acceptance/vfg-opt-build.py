"""Isolated VFG build using the accepted 2.0.5 dependency configuration."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007'
label=sys.argv[1];assert label.replace('-','').isalnum()
BUILD=BASE/'build'/TASK;TMP=BASE/'tmp'/TASK/'build';LOG=BASE/'logs'/TASK/(label+'.log')
for p in (BUILD,TMP,LOG.parent):p.mkdir(parents=True,exist_ok=True)
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')];subprocess.run(guard,check=True)
cache=BASE/'build/release-2.0.5-20261007/standard/CMakeCache.txt';settings=[]
for line in cache.read_text(encoding='utf8').splitlines():
 if ':' not in line or '=' not in line or line.startswith(('#','//')):continue
 keytype,value=line.split('=',1);key,kind=keytype.split(':',1)
 if kind in ('INTERNAL','STATIC') or key in ('VEYRA_DISPLAY_VERSION','VEYRA_AMD_NR_TEST_GRAPH_SOURCE'):continue
 if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH','CMAKE_MAKE_PROGRAM','PROTOC','PKG_CONFIG_EXECUTABLE'):
  settings.append(f'set({key} [[{value}]] CACHE {kind} "Accepted dependencies" FORCE)')
settings.append('set(VEYRA_DISPLAY_VERSION [[2.0.5-vfg-test]] CACHE STRING "Local VFG candidate" FORCE)')
(TMP/'dependencies.cmake').write_text('\n'.join(settings),encoding='utf8')
targets=sys.argv[2:] or ['veyra_qml_ui','veyra_vfg_gpu_tests','veyra_vfg_export_probe','veyra_vfg_settings_tests']
assert all(t.replace('_','').isalnum() for t in targets)
cmd=TMP/'build.cmd';cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 6 --target '+' '.join(targets)+'\nexit /b %errorlevel%\n',encoding='ascii')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),VSLANG='1033',TASK_ROOT=str(ROOT),TASK_BUILD=str(BUILD),TASK_TMP=str(TMP),TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
with LOG.open('x',encoding='utf8') as stream:
 try:rc=subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=890).returncode
 except subprocess.TimeoutExpired:rc=124
LOG.with_suffix('.json').write_text(json.dumps({'exit':rc,'targets':targets,'sourceHead':subprocess.check_output(['git','rev-parse','HEAD'],encoding='ascii').strip(),'dependencyCacheSha256':hashlib.sha256(cache.read_bytes()).hexdigest()},indent=2),encoding='utf8')
print('BUILD',rc,LOG);print('\n'.join(LOG.read_text(encoding='utf8',errors='replace').splitlines()[-12:]));subprocess.run(guard,check=True);raise SystemExit(rc)
