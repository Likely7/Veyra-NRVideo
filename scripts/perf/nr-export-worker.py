"""Existing real worker/queue lifecycle checks, in an isolated ordinary export stage."""
from pathlib import Path
import importlib.util,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_export_workflow_tests.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
payload={str(p.relative_to(app)):matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
results=[]
for mode in ('lifecycle','lifecycle-boundary','queue','clip-hevc-mp4','clip-hevc-mkv'):
 matrix.assert_gpu_tests_idle();name=label+'-'+mode;out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
 for path in (out,tmp):path.mkdir(parents=True,exist_ok=False)
 dest=out/'queue' if mode=='queue' else out/('export.mkv' if 'mkv' in mode else 'export.mp4')
 env=os.environ.copy()
 for key in tuple(env):
  if key.upper().startswith('VEYRA_'):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'engine.log'))
 command=[str(exe),mode,str(matrix.SOURCES['M1']),str(dest)];print('START',name,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
  except subprocess.TimeoutExpired:rc=124
 result={'name':name,'mode':mode,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'passed':rc==0,'extraGpuLoad':False,
  'exeSha256':matrix.digest(exe),'outputs':{str(p.relative_to(out)):matrix.digest(p) for p in out.rglob('*') if p.is_file() and p.suffix in ('.mp4','.mkv')}}
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);print('RESULT',json.dumps(result),flush=True)
 (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
 if not result['passed']:print((out/'console.log').read_text(encoding='utf-8',errors='replace')[-6000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated'
(folder/'summary.json').write_text(json.dumps({'runs':results,'payloadUnchanged':True,'passed':True},ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('EXPORT_WORKER_COMPLETE',len(results),flush=True)
