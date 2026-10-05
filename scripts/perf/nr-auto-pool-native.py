"""Same-session actual-size NR pools versus fresh features, no extra load."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];repeats=int(sys.argv[3]) if len(sys.argv)>3 else 2;assert 1<=repeats<=3
matrix.assert_gpu_tests_idle();folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
app=matrix.stage(variant,label);exe=app/'veyra_nr_auto_pool_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
identity={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8')
results=[];tables={}
for count in (1,2):
 for repeat in range(1,repeats+1):
  for mode in ('reference','pool'):
   name=f'{label}-{count}-{mode}-r{repeat}';out=BASE/'tests'/TASK/name;out.mkdir(parents=True,exist_ok=False)
   tmp=BASE/'tmp'/TASK/name;tmp.mkdir(parents=True,exist_ok=False)
   env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))};env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1')
   matrix.assert_gpu_tests_idle();print('START',name,flush=True);start=time.monotonic()
   with (out/'console.log').open('x',encoding='utf-8') as stream:
    proc=subprocess.run([str(exe),str(matrix.SOURCES['M1']),str(out),mode,str(count)],cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250)
   console=(out/'console.log').read_text(encoding='utf-8',errors='replace');log=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
   nativePass=proc.returncode==0 and f'NR_AUTO_POOL_RESULT pass=1 frames=90 layers={count} debugErrors=0 deviceRemoved=0' in console and '[ERROR]' not in log
   row={'name':name,'mode':mode,'layers':count,'repeat':repeat,'nativePassed':nativePass,'exitCode':proc.returncode,'wallSeconds':time.monotonic()-start,
        'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'exeSha256':matrix.digest(exe),'runtimeSha256':identity['runtime/experimental/nvngx_dlssnr.dll'],
        'sourceCommit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip(),
        'poolEvents':[line for line in log.splitlines() if '[nr-auto-pool]' in line],
        'explicitParameterDestruction':[line for line in log.splitlines() if 'NR parameter blocks explicitly destroyed=' in line]}
   (out/'receipt.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf-8');results.append(row)
   (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');assert nativePass,row
   with (out/'frames.csv').open(encoding='utf-8') as stream:frames=list(csv.DictReader(stream))
   assert len(frames)==90 and [int(frames[i*15]['level']) for i in range(6)]==[0,1,2,3,4,0]
   assert all(int(frames[i*15]['historyReset'])==1 for i in range(6))
   # Save differences before asserting. A failed comparison remains evidence.
   key=(count,'reference',1)
   if mode=='reference' and repeat==1:tables[key]=frames
   different=[i for i,(a,b) in enumerate(zip(tables[key],frames)) if a!=b]
   row.update(referenceDifferentFrames=different,allFullFramePixelsCompared=True)
   (out/'receipt.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf-8')
   (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
   print('AUTO_POOL_NATIVE_RESULT',json.dumps(row,ensure_ascii=False),flush=True)
   assert not different,'Actual-size pool differs from independently-created same-size reference'
assert all(matrix.digest(app/name)==sha for name,sha in identity.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'fullFrameCount':len(results)*90,'referenceDifferentFrames':0,'payloadUnchanged':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('AUTO_POOL_NATIVE_COMPLETE',len(results),flush=True)
