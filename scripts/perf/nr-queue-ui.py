"""Actual UI provider switches, all ordinary playback; no GPU competitor."""
from pathlib import Path
from datetime import datetime
import importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];backends=sys.argv[3:] or ['fsr3','xess','vfg'];assert all(b in ('fsr3','xess','vfg') for b in backends)
matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copy2(ROOT/'scripts/perf/nr-queue-ui.qml',app/'qml/Veyra/QueueUiProbe.qml')
main=app/'qml/Veyra/Main.qml';original=main.read_text(encoding='utf-8');at=original.rfind('}')
main.write_text(original[:at]+'\n Loader{source:"QueueUiProbe.qml";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'queue-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.backend=c.backend}}\n'+original[at:],encoding='utf-8')
results=[]
for backend in backends:
 matrix.assert_gpu_tests_idle();name=f'{label}-{backend}';out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
 for path in (out,profile,tmp):path.mkdir(parents=True,exist_ok=False)
 (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','prewarmEnhancement':False}),encoding='utf-8')
 (app/'queue-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'backend':backend}),encoding='utf-8')
 env=os.environ.copy()
 for key in tuple(env):
  if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1',VEYRA_VERBOSE_FRAME_LOGS='1')
 command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','110000'];print('START',name,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120).returncode
  except subprocess.TimeoutExpired:rc=124
 log=(out/'player.log').read_text(encoding='utf-8',errors='replace');phases=[json.loads(line.split('QUEUE_UI_PHASE ',1)[1]) for line in log.splitlines() if 'QUEUE_UI_PHASE ' in line]
 queues=[line for line in log.splitlines() if '[graph-queue]' in line or 'queue-rebind type=' in line]
 selected=re.findall(r'\[graph-queue\] selected=(COMPUTE|DIRECT)',log)
 expected=['COMPUTE','DIRECT','COMPUTE','DIRECT','DIRECT','COMPUTE','DIRECT','COMPUTE']
 changes=[(datetime.fromisoformat(line.split()[0].replace('Z','+00:00')).timestamp()*1000,re.search(r'selected=(COMPUTE|DIRECT)',line)[1]) for line in queues if 'selected=' in line]
 actual=[next((queue for at,queue in reversed(changes) if at<=phase['at']),None) for phase in phases[1:]]
 passed=rc==0 and 'QUEUE_UI_PASS' in log and len(phases)==9 and actual==expected and not any(s in log for s in ('QUEUE_UI_FAIL','[ERROR]','[FATAL]','ReferenceError:','TypeError:','leaked parameter block'))
 result={'name':name,'backend':backend,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'phases':phases,'queueLines':queues,'actualQueues':selected,'steadyPhaseQueues':actual,'expectedPhaseQueues':expected,'passed':passed,'extraGpuLoad':False}
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);print('RESULT',name,passed,selected,flush=True)
 if not passed:print('\n'.join(s for s in log.splitlines() if 'QUEUE_UI_' in s or '[ERROR]' in s or '[graph-queue]' in s or 'backend-recovery' in s)[-6500:],flush=True);raise SystemExit(1)
(BASE/'logs'/TASK/(label+'-summary.json')).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('QUEUE_UI_COMPLETE',len(results),flush=True)
