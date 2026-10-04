"""Directed safety checks for the export candidate; no pressure or device removal."""
from pathlib import Path
import importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_export_pipeline_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
payload={str(p.relative_to(app)):matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
active={'VEYRA_TEST_EXPORT_ASYNC':'1','VEYRA_TEST_EXPORT_FENCE_EVENTS':'1'}
hdr=BASE/'tests/video-hdr-20260918/hdr-export.mp4'
cases=[('single-off','none',1,{},False,None),('single-on','none',1,active,False,None),
 ('nr2-debug-off','nr2',24,{'VEYRA_TEST_EXPORT_DEBUG':'1'},False,None),('nr2-debug-on','nr2',24,{**active,'VEYRA_TEST_EXPORT_DEBUG':'1'},False,None),
 ('nrsr-debug-off','nrsr4k',24,{'VEYRA_TEST_EXPORT_DEBUG':'1'},False,None),('nrsr-debug-on','nrsr4k',24,{**active,'VEYRA_TEST_EXPORT_DEBUG':'1'},False,None),
 ('fg-off','fg2',40,{},False,None),('fg-on','fg2',40,active,False,None),
 ('cancel-off','nrsr4k',24,{},True,None),('cancel-on','nrsr4k',24,active,True,None),
 ('failure-reference','nr',32,{},False,None),
 ('event-create-failure','nr',32,{**active,'VEYRA_TEST_EXPORT_EVENT_CREATE_FAIL':'1'},False,None),
 ('event-register-failure','nr',32,{**active,'VEYRA_TEST_EXPORT_EVENT_REGISTER_FAIL':'1'},False,None),
 ('hdr-off','none',16,{'VEYRA_TEST_EXPORT_DEBUG':'1'},False,hdr),('hdr-on','none',16,{**active,'VEYRA_TEST_EXPORT_DEBUG':'1'},False,hdr)]
results=[]
for name,group,frames,flags,cancel,source in cases:
 matrix.assert_gpu_tests_idle();source=source or matrix.SOURCES['M1'];run=label+'-'+name;out=BASE/'logs'/TASK/run;tmp=BASE/'tmp'/TASK/run
 for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
 dest=out/'export.mp4';env=os.environ.copy()
 for key in tuple(env):
  if key.upper().startswith('VEYRA_'):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),**flags)
 command=[str(exe),str(source),str(dest),group,str(frames),str(out)]+(['--cancel'] if cancel else [])
 print('START',run,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
  except subprocess.TimeoutExpired:rc=124
 text=(out/'console.log').read_text(encoding='utf-8',errors='replace');log=(out/'engine.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('EXPORT_PIPELINE_RESULT ')),'')
 metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)}
 passed=rc==0 and (metrics.get('cancelled')==1 and metrics.get('ok')==0 and not dest.exists() if cancel else metrics.get('ok')==1 and metrics.get('source')==frames)
 if 'VEYRA_TEST_EXPORT_DEBUG' in flags:passed=passed and '[export-debug] errors=0 removedReason=0x0' in log
 if name=='fg-on':passed=passed and 'mode=serial fenceEvents=true maxInFlight=1' in log
 if 'failure' in name and name!='failure-reference':passed=passed and 'injected=true; keeping polling' in log and 'fenceEvents=false maxInFlight=2' in log
 if cancel:passed=passed and not list(out.glob('*.partial*'))
 result={'name':run,'case':name,'group':group,'command':command,'flags':flags,'exitCode':rc,'metrics':metrics,'wallSeconds':time.monotonic()-start,'sourceSha256':matrix.digest(source),
  'outputSha256':matrix.digest(dest) if dest.exists() else None,'exeSha256':matrix.digest(exe),'passed':passed,'extraGpuLoad':False,
  'diagnostics':[s for s in log.splitlines() if '[export-debug]' in s or '[export-pipeline]' in s]}
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
 print('RESULT',json.dumps(result),flush=True)
 if not passed:print(log[-7000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated'
lookup={r['case']:r for r in results};pairs=[('single-off','single-on'),('nr2-debug-off','nr2-debug-on'),('nrsr-debug-off','nrsr-debug-on'),('fg-off','fg-on'),('hdr-off','hdr-on'),
 ('failure-reference','event-create-failure'),('failure-reference','event-register-failure')]
comparisons=[{'a':a,'b':b,'sameWholeFile':lookup[a]['outputSha256']==lookup[b]['outputSha256']} for a,b in pairs]
passed=all(c['sameWholeFile'] for c in comparisons);(folder/'summary.json').write_text(json.dumps({'runs':results,'comparisons':comparisons,'passed':passed,'payloadUnchanged':True},ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('EXPORT_EDGES_COMPLETE',passed,comparisons,flush=True);raise SystemExit(0 if passed else 1)
