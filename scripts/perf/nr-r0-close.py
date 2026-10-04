"""Close during ordinary user-requested provider/NR rebuild; no forced GPU fault."""
from pathlib import Path
import importlib.util, json, os, shutil, subprocess, sys, time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];selected=sys.argv[3:];matrix.assert_gpu_tests_idle();folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(exist_ok=False)
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
if variant != 'A':shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copy2(ROOT/'scripts/perf/nr-r0-close.qml',app/'qml/Veyra/CloseProbe.qml')
main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf-8');at=text.rfind('}')
main.write_text(text[:at]+'\n Loader { source:"CloseProbe.qml"; onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'close-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.action=c.action} }\n'+text[at:],encoding='utf-8')
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8');results=[]
actions=('fsr3','xess','vfg','nr-runtime','nr-size','nr-layer','stop')
if selected:
 assert all(action in actions for action in selected)
 actions=tuple(action for action in actions if action in selected)
for action in actions:
 matrix.assert_gpu_tests_idle();name=label+'-'+action;out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name;profile=BASE/'tests'/TASK/name/'profile'
 for path in (out,tmp,profile):path.mkdir(parents=True,exist_ok=False)
 (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','prewarmEnhancement':False,'gpuPriority':'normal'}),encoding='utf-8')
 (app/'close-run.json').write_text(json.dumps({'action':action,'media':str(matrix.SOURCES['M1']).replace('\\','/')}),encoding='utf-8')
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',QML_XHR_ALLOW_FILE_READ='1',QT_FORCE_STDERR_LOGGING='1')
 command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','60000'];start=time.monotonic();print('R0_CLOSE_START',action,flush=True)
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=70).returncode
  except subprocess.TimeoutExpired:rc=124
 text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
 errors=[line for line in text.splitlines() if any(value in line for value in ('[ERROR]','[FATAL]','CLOSE_UI_FAIL','ReferenceError:','TypeError:','leaked parameter block'))]
 row={'action':action,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'quitEvidence':[line for line in text.splitlines() if 'CLOSE_UI_QUIT' in line],'errors':errors,
  'passed':rc==0 and 'CLOSE_UI_PASS' in text and not errors,'pressure':False}
 results.append(row);(out/'result.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf-8');(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
 print('R0_CLOSE_RESULT',action,row['passed'],rc,flush=True)
 if not row['passed']:print(text[-5000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'passed':True,'payloadUnchanged':True,'pressure':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('R0_CLOSE_COMPLETE',len(results),flush=True)
