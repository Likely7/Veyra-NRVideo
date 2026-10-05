"""Real Qt layout/fullscreen/subtitle/rate regression in both product render modes."""
from pathlib import Path
import importlib.util,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');shutil.copytree(matrix.PACKAGE,app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
main=app/'qml/Veyra/Main.qml';original=main.read_text(encoding='utf-8');at=original.rfind('}');folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
results=[];exeHash=matrix.digest(app/'veyra_qml_ui.exe');source=BASE/'tests/playback-smoothness-20261004/fixtures/golden.mkv'
for renderer in ('gpu','obs-compat'):
 for mode in ('functional','layout'):
  matrix.assert_gpu_tests_idle();name=f'{label}-{renderer}-{mode}';out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
  for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
  (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','prewarmEnhancement':False}),encoding='utf-8')
  fixture=(ROOT/'scripts/acceptance'/('playback-smoothness-'+mode+'.qml')).read_text(encoding='utf-8')
  # The reusable fixture must stop after reporting a failure, even if Qt.quit
  # is asynchronous. Its authored tests and assertions are otherwise unchanged.
  probe=app/'qml/Veyra/R0UiProbe.qml';probe.write_text(fixture.replace('import Veyra\n',''),encoding='utf-8')
  loader='\n Loader{source:"R0UiProbe.qml";onLoaded:{item.media='+json.dumps(str(source).replace('\\','/'))+';item.mode='+json.dumps(mode)+'}}\n'
  main.write_text(original[:at]+loader+original[at:],encoding='utf-8')
  payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
  (out/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
  env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
  env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
  args=['--obs-game-capture'] if renderer=='obs-compat' else []
  command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','150000',*args];print('START',name,flush=True);start=time.monotonic()
  with (out/'console.log').open('xb') as stream:
   try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=160).returncode
   except subprocess.TimeoutExpired:rc=124
  log=(out/'player.log').read_text(encoding='utf-8',errors='replace');marker='PLAYBACK_FUNCTION_PASS' if mode=='functional' else 'PLAYBACK_LAYOUT_FULLSCREEN_PASS'
  errors=[s for s in log.splitlines() if any(x in s for x in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','FUNCTION_FAIL','LAYOUT_FAIL'))]
  row={'name':name,'renderer':renderer,'mode':mode,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'exeSha256':exeHash,'sourceSha256':matrix.digest(source),
   'markers':[s for s in log.splitlines() if 'PLAYBACK_' in s and 'PASS' in s],'errors':errors,'passed':rc==0 and marker in log and not errors,'extraGpuLoad':False,
   'actualObsCaptureTested':False,'obsCompatSelected':renderer=='obs-compat' and 'OBS game capture compatibility: software UI' in log}
  assert renderer!='obs-compat' or row['obsCompatSelected']
  assert all(matrix.digest(app/n)==sha for n,sha in payload.items());results.append(row);(out/'result.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf-8')
  (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,row['passed'],flush=True)
  if not row['passed']:print('\n'.join(errors)[-4000:],log[-3000:],flush=True);raise SystemExit(1)
main.write_text(original,encoding='utf-8');matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'passed':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8');print('R0_UI_COMPLETE',len(results),flush=True)
