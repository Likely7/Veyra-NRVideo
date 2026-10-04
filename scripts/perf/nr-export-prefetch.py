"""Small real-export prefetch/output/debug checks before any timing matrix."""
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
results=[]
for group,prefetch,cancel in [('nr',False,False),('nr',True,False),('nrsr4k',False,False),('nrsr4k',True,False),('nr2',False,False),('nr2',True,False),('nr',True,True)]:
 matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{int(prefetch)}'+('-cancel' if cancel else '');out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
 for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
 dest=out/'export.mp4';env=os.environ.copy()
 for key in tuple(env):
  if key.upper().startswith('VEYRA_'):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_TEST_EXPORT_DEBUG='1')
 if prefetch:env['VEYRA_TEST_EXPORT_NVOF_PREFETCH']='1'
 command=[str(exe),str(matrix.SOURCES['M1']),str(dest),group,'24',str(out)]+(['--cancel'] if cancel else [])
 print('START',name,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:
  try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
  except subprocess.TimeoutExpired:rc=124
 text=(out/'console.log').read_text(encoding='utf-8',errors='replace');log=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
 line=next((s for s in text.splitlines() if s.startswith('EXPORT_PIPELINE_RESULT ')),'');metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)}
 passed=rc==0 and '[export-debug] errors=0 removedReason=0x0' in log and (metrics.get('cancelled')==1 and metrics.get('ok')==0 and not dest.exists() if cancel else metrics.get('ok')==1 and metrics.get('source')==24)
 if prefetch and not cancel:passed=passed and '[flow-prefetch] source=24 consumed=' in log
 result={'name':name,'group':group,'prefetch':prefetch,'cancel':cancel,'command':command,'exitCode':rc,'metrics':metrics,'passed':passed,'extraGpuLoad':False,
  'wallSeconds':time.monotonic()-start,'exeSha256':matrix.digest(exe),'outputSha256':matrix.digest(dest) if dest.exists() else None,
  'diagnostics':[s for s in log.splitlines() if any(tag in s for tag in ('[export-debug]','[flow-prefetch]','[export-pipeline]','[scene]'))]}
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
 (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',json.dumps(result),flush=True)
 if not passed:print(log[-7000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated'
pairs=[{'group':g,'sameWholeFile':len({r['outputSha256'] for r in results if r['group']==g and not r['cancel']})==1} for g in ('nr','nrsr4k','nr2')]
passed=all(p['sameWholeFile'] for p in pairs)
(folder/'summary.json').write_text(json.dumps({'runs':results,'pairs':pairs,'passed':passed,'payloadUnchanged':True},ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('PREFETCH_SMOKE_COMPLETE',passed,pairs,flush=True);raise SystemExit(0 if passed else 1)
