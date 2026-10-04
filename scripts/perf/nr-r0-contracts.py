"""Directed product regression; no pressure/competition/device removal fixtures."""
from pathlib import Path
import ctypes,importlib.util,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];selected=sys.argv[3:];matrix.assert_gpu_tests_idle();folder=BASE/'logs'/TASK/label;folder.mkdir(parents=True,exist_ok=False)
app=BASE/'tests'/TASK/(label+'-app');shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copytree(ROOT/'tests/qml/quick',app/'qml-tests');qt=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
for dll in ('Qt6QuickTest.dll','Qt6Test.dll'):
 if not (app/dll).exists():shutil.copy2(qt/'bin'/dll,app/dll)
shutil.copytree(qt/'qml/QtTest',app/'qml/QtTest',copy_function=matrix.copy_dependency)
shutil.copy2(qt/'plugins/platforms/qoffscreen.dll',app/'platforms/qoffscreen.dll')
# Only this Python parent and its test children inherit no crash dialog. No
# Windows setting or unrelated process is changed.
ctypes.windll.kernel32.SetErrorMode(0x0002)
fixtures=BASE/'tests/playback-smoothness-20261004/fixtures';assert (fixtures/'tone.wav').is_file()
cases=[('repair','veyra_repair_contract_tests',[]),
 ('present-sink','veyra_present_sink_lifecycle_tests',[str(BASE/'logs'/TASK/label/'present-sink')]),
 ('xbox','veyra_xbox_tests',[]),('effect-chain','veyra_effect_chain_tests',[]),
 ('preset','veyra_repair_preset_tests',[str(BASE/'tests'/TASK/label/'presets.v1')]),('availability','veyra_effect_availability_tests',[]),
 ('ui-contract','veyra_ui_contract_tests',[str(BASE/'tests'/TASK/label/'ui-contract')]),('vfg-settings','veyra_vfg_settings_tests',[str(BASE/'tests'/TASK/label/'vfg-settings')]),
 ('auto-controller','veyra_nr_auto_controller_tests',[]),('i18n','veyra_ui_i18n_tests',[]),
 ('qml-data','veyra_qml_data_tests',[str(BASE/'tests'/TASK/label/'qml-data')]),('qml-easing','veyra_qml_easing_tests',[]),
 ('qml-quick','veyra_qml_quick_tests',[]),('audio-rates','veyra_audio_playback_rate_tests',[str(fixtures/'tone.wav')]),
 ('subtitle-text','veyra_subtitle_text_tests',[str(fixtures)]),('subtitle-overlay','veyra_subtitle_overlay_tests',[])]
if selected:
 assert all(name in {c[0] for c in cases} for name in selected)
 cases=[c for c in cases if c[0] in selected]
for _,target,_ in cases:shutil.copy2(BASE/'build'/TASK/variant/(target+'.exe'),app/(target+'.exe'))
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8');results=[]
for suffix,target,args in cases:
 matrix.assert_gpu_tests_idle();name=label+'-'+suffix;tmp=BASE/'tmp'/TASK/name;tmp.mkdir(parents=True,exist_ok=False)
 scratch=BASE/'tests'/TASK/label;scratch.mkdir(parents=True,exist_ok=True)
 (scratch/'qml-export').mkdir(exist_ok=True)
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(folder/(suffix+'-engine.log')),VEYRA_DATA_DIR=str(scratch/suffix),
  VEYRA_EXPORT_TEST_ARTIFACTS=str(scratch/'qml-export'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
 if suffix=='qml-quick':env['QT_QUICK_BACKEND']='software'
 command=[str(app/(target+'.exe')),*args];print('START',name,flush=True);start=time.monotonic()
 with (folder/(suffix+'.log')).open('xb') as stream:
  try:rc=subprocess.run(command,cwd=tmp,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
  except subprocess.TimeoutExpired:rc=124
 row={'name':name,'command':command,'exitCode':rc,'passed':rc==0,'wallSeconds':time.monotonic()-start,'exeSha256':matrix.digest(app/(target+'.exe')),'extraGpuLoad':False}
 results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,rc,flush=True)
 if rc:print((folder/(suffix+'.log')).read_text(encoding='utf-8',errors='replace')[-9000:],flush=True)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'payloadUnchanged':True,'passed':all(r['passed'] for r in results),'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('R0_CONTRACTS_COMPLETE',all(r['passed'] for r in results),len(results),flush=True);raise SystemExit(0 if all(r['passed'] for r in results) else 1)
