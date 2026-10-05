"""Auto NR actual-size quality, default-off frames and native export boundaries."""
from pathlib import Path
import csv,importlib.util,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
app=matrix.stage(variant,label)
targets=['veyra_nr_auto_pool_experiment','veyra_nr_mixed_resolution_experiment','veyra_nr_export_pipeline_experiment']
for target in targets:shutil.copy2(BASE/'build'/TASK/variant/(target+'.exe'),app/(target+'.exe'))
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8');results=[];tables={};exportReference=None
cases=[('auto-'+mode,targets[0],mode) for mode in ('reference','pool','fixed')]+[('off-'+g,targets[1],g) for g in ('none','2-native','2-mixed','3-native','3-mixed')]+[('export-'+g,targets[2],g) for g in ('nr2','nr2-auto')]
for suffix,target,mode in cases:
 matrix.assert_gpu_tests_idle();name=label+'-'+suffix;out=BASE/'tests'/TASK/name;tmp=BASE/'tmp'/TASK/name
 for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))};env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1')
 args=[str(matrix.SOURCES['M1']),str(out),mode,'2'] if suffix.startswith('auto-') else [str(matrix.SOURCES['M1']),str(out),mode]
 if suffix.startswith('export-'):args=[str(matrix.SOURCES['M1']),str(out/'export.mkv'),mode,'120',str(out)]
 command=[str(app/(target+'.exe')),*args];print('START',name,flush=True);start=time.monotonic()
 with (out/'console.log').open('xb') as stream:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
 console=(out/'console.log').read_text(encoding='utf-8',errors='replace');log=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
 expected='NR_AUTO_POOL_RESULT pass=1 frames=90 layers=2 debugErrors=0 deviceRemoved=0' if suffix.startswith('auto-') else 'NR_MIXED_RESULT pass=1 frames=40 debugErrors=0 deviceRemoved=0' if suffix.startswith('off-') else 'EXPORT_PIPELINE_RESULT ok=1 cancelled=0 source=120 generated=0 hold=0 encoded=120'
 row={'name':name,'mode':mode,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'passed':rc==0 and expected in console and '[ERROR]' not in log,
  'exeSha256':matrix.digest(app/(target+'.exe')),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'runtimeSha256':payload['runtime/experimental/nvngx_dlssnr.dll'],
  'driverSourceHead':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'extraGpuLoad':False}
 results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');assert row['passed'],row
 if not suffix.startswith('export-'):
  with (out/'frames.csv').open(encoding='utf-8') as stream:frames=list(csv.DictReader(stream))
  if suffix=='auto-reference':tables['reference']=frames
  if suffix=='auto-pool':assert frames==tables['reference'],'Pool/reference whole RGBA/PTS differs'
  if suffix=='auto-fixed':
   assert all(a['sha256']==b['sha256'] and a['pts100ns']==b['pts100ns'] for a,b in zip(frames[:15],tables['reference'][:15])),'Auto 100% differs from fixed 100%'
   row['lowerExtentChangedFrames']=sum(a['sha256']!=b['sha256'] for a,b in zip(frames,tables['reference']))
  if suffix.startswith('off-'):
   prior=BASE/'tests'/TASK/f'B4b-native-v3-{mode}-r1'/'frames.csv'
   with prior.open(encoding='utf-8') as stream:old=list(csv.DictReader(stream))
   assert frames==old,'Default-off full-frame/source/history contract changed'
   row['comparedBaseline']=str(prior);row['differentWholeRgbaFrames']=0
 else:
  row['outputSha256']=matrix.digest(out/'export.mkv');row['poolCreated']='event=ready' in log and '[nr-auto-pool]' in log;assert not row['poolCreated']
  if exportReference is None:exportReference=row['outputSha256']
  else:assert exportReference==row['outputSha256'],'Native export differs with Auto selected'
 (out/'receipt.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf-8');(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,row['passed'],flush=True)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'payloadUnchanged':True,'nativeFullFrameCount':470,'wholeExportHashSame':True,'extraGpuLoad':False,'humanQualityApproval':False},ensure_ascii=False,indent=2),encoding='utf-8');print('AUTO_FINAL_COMPLETE',len(results),flush=True)
