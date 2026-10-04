"""Normal production exports; no extra load. Decode verification runs after timing."""
from pathlib import Path
import importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label,mode=sys.argv[1:4];groups=sys.argv[4:] or ['none4k','nr','nr2','nrsr4k','sr8k']
assert mode in ('default','serial','events','async','async-events','prefetch','prefetch-trace','interleaved','default-interleaved','prefetch-interleaved','all')
assert all(g in ('none4k','nr','nr2','sr4k','nrsr4k','sr8k','fg2') for g in groups)
matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_export_pipeline_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
payload={str(p.relative_to(app)):matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
results=[]
for group in groups:
 source=BASE/'tests/perf-matrix/media/M2.mkv' if group in ('none4k','sr8k') else matrix.SOURCES['M1']
 config='none' if group=='none4k' else group;frames=60 if group=='sr8k' else 120
 for repeat in (range(1,2) if mode=='prefetch-trace' else range(1,4)):
  modes=(('default','prefetch') if repeat%2 else ('prefetch','default')) if mode=='prefetch-interleaved' else (('serial','default') if repeat%2 else ('default','serial')) if mode=='default-interleaved' else (('serial','async') if repeat%2 else ('async','serial')) if mode=='interleaved' else (('serial','events','async','async-events') if repeat%2 else ('async-events','async','events','serial')) if mode=='all' else (mode,)
  for current in modes:
   matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{current}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
   for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
   dest=out/'export.mkv';env=os.environ.copy()
   for key in tuple(env):
    if key.upper().startswith('VEYRA_'):env.pop(key)
   env.update(TEMP=str(tmp),TMP=str(tmp))
   if current=='serial':env['VEYRA_TEST_EXPORT_SERIAL']='1'
   if current=='events':env['VEYRA_TEST_EXPORT_ASYNC']='0'
   if current=='async':env['VEYRA_TEST_EXPORT_FENCE_EVENTS']='0'
   if current in ('prefetch','prefetch-trace'):env['VEYRA_TEST_EXPORT_NVOF_PREFETCH']='1'
   if current=='prefetch-trace':env['VEYRA_TEST_EXPORT_PREFETCH_TIMESTAMPS']='1'
   if current in ('async','async-events'):env['VEYRA_TEST_EXPORT_ASYNC']='1'
   if current in ('events','async-events'):env['VEYRA_TEST_EXPORT_FENCE_EVENTS']='1'
   command=[str(exe),str(source),str(dest),config,str(frames),str(out)]
   before=matrix.gpu_query();print('START',name,flush=True);start=time.monotonic()
   with (out/'console.log').open('xb') as stream:
    try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
    except subprocess.TimeoutExpired:rc=124
   elapsed=time.monotonic()-start;after=matrix.gpu_query()
   text=(out/'console.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('EXPORT_PIPELINE_RESULT ')),'')
   metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)};log=(out/'engine.log').read_text(encoding='utf-8',errors='replace') if (out/'engine.log').exists() else ''
   pipeline=[s for s in log.splitlines() if '[export-pipeline]' in s];stages=[s for s in log.splitlines() if '[graph-contract]' in s or '[resolution]' in s or '[export-counts]' in s]
   result={'name':name,'group':group,'mode':current,'repeat':repeat,'command':command,'exitCode':rc,'metrics':metrics,'wallSeconds':elapsed,'beforeGpu':before,'afterGpu':after,
    'pipeline':pipeline,'stages':stages,'exeSha256':matrix.digest(exe),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),
    'sourceSha256':matrix.digest(source),'outputSha256':matrix.digest(dest) if dest.exists() else None,
    'driverSourceHead':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
    'passed':rc==0 and metrics.get('ok')==1 and metrics.get('source')==frames and metrics.get('encoded')==frames*(2 if group=='fg2' else 1) and bool(pipeline),
    'extraGpuLoad':False,'note':'Normal export includes enhancement and encoder drain; no artificial GPU load or memory pressure.'}
   (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
   (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,json.dumps({'passed':result['passed'],'metrics':metrics,'pipeline':pipeline}),flush=True)
   if not result['passed']:print(text[-5000:],log[-7000:],flush=True);raise SystemExit(1)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated'
matrix.assert_gpu_tests_idle();(folder/'summary.json').write_text(json.dumps({'runs':results,'payloadUnchanged':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('EXPORT_PIPELINE_COMPLETE',len(results),flush=True)
