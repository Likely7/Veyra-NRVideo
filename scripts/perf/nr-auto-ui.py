"""Real product Auto selector/lifecycle/persistence/fallback. Ordinary playback only."""
from pathlib import Path
import importlib.util,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];modes=sys.argv[3:] or ['lifecycle','persist','refuse'];assert all(m in ('lifecycle','persist','refuse') for m in modes)
matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(matrix.PACKAGE,app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copy2(ROOT/'scripts/perf/nr-auto-ui.qml',app/'qml/Veyra/AutoUiProbe.qml')
main=app/'qml/Veyra/Main.qml';original=main.read_text(encoding='utf-8');at=original.rfind('}')
main.write_text(original[:at]+'\n Loader{source:"AutoUiProbe.qml";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'auto-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.mode=c.mode}}\n'+original[at:],encoding='utf-8')
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8');results=[]
for mode in modes:
 matrix.assert_gpu_tests_idle();name=f'{label}-{mode}';out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/(label+'-profile' if mode in ('lifecycle','persist') else name+'-profile');tmp=BASE/'tmp'/TASK/name
 for path in (out,tmp):path.mkdir(parents=True,exist_ok=False)
 if mode!='persist':
  profile.mkdir(parents=True,exist_ok=False)
  (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','prewarmEnhancement':False}),encoding='utf-8')
 else:assert profile.exists(),'Run lifecycle first to save real product configuration'
 (app/'auto-run.json').write_text(json.dumps({'media':str(BASE/'tests/perf-matrix/media/M10-1080p60-synthetic.mkv').replace('\\','/'),'mode':mode}),encoding='utf-8')
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1',VEYRA_VERBOSE_FRAME_LOGS='1')
 if mode=='refuse':env['VEYRA_TEST_NR_AUTO_POOL_REFUSE']='1'
 command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','190000'];print('START',name,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=200).returncode
  except subprocess.TimeoutExpired:rc=124
 log=(out/'player.log').read_text(encoding='utf-8',errors='replace');console=(out/'console.log').read_text(encoding='utf-8',errors='replace')
 phases=[json.loads(line.split('AUTO_UI_PHASE ',1)[1]) for line in log.splitlines() if 'AUTO_UI_PHASE ' in line]
 errors=[line for line in (log+'\n'+console).splitlines() if any(s in line for s in ('AUTO_UI_FAIL','[ERROR]','[FATAL]','ReferenceError:','TypeError:','leaked parameter block'))]
 result={'name':name,'mode':mode,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),
  'sourceCommit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'runtimeSha256':payload['runtime/experimental/nvngx_dlssnr.dll'],
  'phases':phases,'poolEvents':[s for s in log.splitlines() if '[nr-auto' in s],'destruction':[s for s in log.splitlines() if 'NR parameter blocks explicitly destroyed=' in s],
  'errors':errors,'passed':rc==0 and 'AUTO_UI_PASS' in log and not errors,'extraGpuLoad':False,'refusalIsLogicalApiFixture':mode=='refuse'}
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
 (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,result['passed'],[p['name'] for p in phases],flush=True)
 if not result['passed']:print('\n'.join(errors)[-6000:],log[-6000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'payloadUnchanged':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8');print('AUTO_UI_COMPLETE',len(results),flush=True)
