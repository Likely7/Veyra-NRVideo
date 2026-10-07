"""Build standard and RemotePlay-off configurations with approved dependencies."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007'
label=sys.argv[1];assert label.replace('-','').isalnum()
remote_off='--remote-off' in sys.argv
BUILD=BASE/'build'/TASK/('remote-off' if remote_off else 'standard')
TMP=BASE/'tmp'/TASK/('build-off' if remote_off else 'build')
LOG=BASE/'logs'/TASK/(label+'.log')
for p in (BUILD,TMP,LOG.parent):p.mkdir(parents=True,exist_ok=True)
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.5-control.py')]
subprocess.run(guard,check=True)
settings=[];cache=BASE/'build/list-preset-flow-20261006/CMakeCache.txt'
for line in cache.read_text(encoding='utf8').splitlines():
    if ':' not in line or '=' not in line or line.startswith(('#','//')):continue
    keytype,value=line.split('=',1);key,kind=keytype.split(':',1)
    if kind in ('INTERNAL','STATIC') or key=='VEYRA_DISPLAY_VERSION':continue
    if key.startswith('VEYRA_') or key in ('CMAKE_PREFIX_PATH','CMAKE_MAKE_PROGRAM','PROTOC','PKG_CONFIG_EXECUTABLE'):
        if key=='VEYRA_LIBASS_ROOT':assert (Path(value)/'include/ass/ass.h').is_file()
        settings.append(f'set({key} [[{value}]] CACHE {kind} "Accepted dependencies" FORCE)')
settings.append('set(VEYRA_DISPLAY_VERSION [[2.0.5]] CACHE STRING "Local integration" FORCE)')
settings.append('set(VEYRA_AMD_NR_TEST_GRAPH_SOURCE [['+(ROOT/'src/pipeline/EnhanceGraph.cpp').as_posix()+']] CACHE FILEPATH "Identity diagnostic graph" FORCE)')
if remote_off:settings.append('set(VEYRA_ENABLE_REMOTEPLAY OFF CACHE BOOL "PR20 build validation" FORCE)')
(TMP/'dependencies.cmake').write_text('\n'.join(settings),encoding='utf8')
targets=[t for t in sys.argv[2:] if t!='--remote-off'] or (['veyra_qml_ui'] if remote_off else [
    'veyra_qml_ui','veyra_hdr_tuning_tests','veyra_effect_chain_tests','veyra_preset_library_tests','veyra_ui_i18n_tests','veyra_effect_availability_tests',
    'veyra_export_workflow_tests','veyra_amd_nr_graph_tests','veyra_lmxxf_nr_tests',
    'veyra_repair_contract_tests','veyra_live_timing_tests','veyra_fg_presentation_tests','veyra_qml_quick_tests'])
assert all(t.replace('_','').isalnum() for t in targets)
cmd=TMP/'build.cmd'
cmd.write_text('@echo off\nchcp 65001 >nul\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\n"%TASK_CMAKE%" --preset x64-release -S "%TASK_ROOT%" -B "%TASK_BUILD%" -C "%TASK_TMP%\\dependencies.cmake"\nif errorlevel 1 exit /b 5\n"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel 6 --target '+' '.join(targets)+'\nexit /b %errorlevel%\n',encoding='ascii')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),VSLANG='1033',TASK_ROOT=str(ROOT),TASK_BUILD=str(BUILD),TASK_TMP=str(TMP),
    TASK_CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
with LOG.open('x',encoding='utf8') as out:
    try:rc=subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=890).returncode
    except subprocess.TimeoutExpired:rc=124
receipt={'exit':rc,'log':str(LOG),'targets':targets,'remotePlayOff':remote_off,
    'sourceHead':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
    'dependencyCacheSha256':hashlib.sha256(cache.read_bytes()).hexdigest()}
LOG.with_suffix('.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print('BUILD',rc,LOG);print('\n'.join(LOG.read_text(encoding='utf8',errors='replace').splitlines()[-13:]))
subprocess.run(guard,check=True);raise SystemExit(rc)
