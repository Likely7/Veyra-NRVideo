"""Actual-size crop diagnostic, ordinary M1 pixels; no product ROI claim."""
from pathlib import Path
import csv,importlib.util,json,os,shutil,statistics,subprocess,sys,time
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];repeats=int(sys.argv[3]) if len(sys.argv)>3 else 2;assert 1<=repeats<=3
matrix.assert_gpu_tests_idle();folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
app=matrix.stage(variant,label);exe=app/'veyra_nr_letterbox_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
results=[];references={}
for shape in ('239','43'):
 for repeat in range(1,repeats+1):
  for mode in ('none','full','crop'):
   name=f'{label}-{shape}-{mode}-r{repeat}';out=BASE/'tests'/TASK/name;out.mkdir(parents=True,exist_ok=False)
   tmp=BASE/'tmp'/TASK/name;tmp.mkdir(parents=True,exist_ok=False)
   env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))};env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1')
   matrix.assert_gpu_tests_idle();print('START',name,flush=True);start=time.monotonic()
   with (out/'console.log').open('x',encoding='utf-8') as stream:
    proc=subprocess.run([str(exe),str(matrix.SOURCES['M1']),str(out),mode,shape],cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250)
   console=(out/'console.log').read_text(encoding='utf-8',errors='replace');log=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
   passed=proc.returncode==0 and 'LETTERBOX_RESULT pass=1 frames=40 debugErrors=0 deviceRemoved=0' in console and '[ERROR]' not in log
   row={'name':name,'shape':shape,'mode':mode,'repeat':repeat,'passed':passed,'exitCode':proc.returncode,'wallSeconds':time.monotonic()-start,
        'exeSha256':matrix.digest(exe),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'sourceCommit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip(),
        'stimulus':'Authored central crop of ordinary M1, black padding and late burned-in text. Not real letterboxed film or console capture.'}
   results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');assert passed,row
   with (out/'frames.csv').open(encoding='utf-8') as stream:frames=list(csv.DictReader(stream))
   assert len(frames)==40
   signature=[{k:r[k] for k in ('frame','pts100ns','width','height','sha256')} for r in frames]
   if repeat==1:references[(shape,mode)]=signature
   else:assert references[(shape,mode)]==signature,'Repeat full-frame pixel/PTS mismatch'
   warm=[r for r in frames if int(r['frame']) in list(range(6,20))+list(range(26,40))]
   row['gpuWarmMedianMs']={k:statistics.median(float(r[k]) for r in warm) for k in ('enhancementMs','nrMs','flowMs')}
   if mode=='crop':
    plain=BASE/'tests'/TASK/f'{label}-{shape}-none-r{repeat}';full=BASE/'tests'/TASK/f'{label}-{shape}-full-r{repeat}'
    x,y,w,h=(0,138,1920,804) if shape=='239' else (240,0,1440,1080)
    mask=np.ones((1080,1920),dtype=bool);mask[y:y+h,x:x+w]=False
    quality=[]
    for i in range(40):
     a=np.asarray(Image.open(full/f'frame-{i}.png').convert('RGBA'));b=np.asarray(Image.open(out/f'frame-{i}.png').convert('RGBA'));p=np.asarray(Image.open(plain/f'frame-{i}.png').convert('RGBA'))
     active=(a[y:y+h,x:x+w,:3].astype(np.float64)-b[y:y+h,x:x+w,:3])
     mse=float(np.mean(active**2));quality.append({'frame':i,'activePsnrDb':float(10*np.log10(255**2/mse)) if mse else None,
      'activeDifferentRgbPixels':int(np.count_nonzero(np.any(active!=0,axis=2))),
      'activeMaxDifference':int(np.abs(active).max()),'outsideDifferentFromSourcePixels':int(np.count_nonzero(np.any(b!=p,axis=2)&mask)),
      'outsideDifferentFromFullNrPixels':int(np.count_nonzero(np.any(a!=b,axis=2)&mask))})
    row['quality']=quality;assert not any(q['outsideDifferentFromSourcePixels'] for q in quality)
   (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
   print('LETTERBOX_NATIVE_RESULT',json.dumps({k:v for k,v in row.items() if k!='quality'},ensure_ascii=False),flush=True)
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'fullFrameCount':len(results)*40,'repeatDifferentFrames':0,
 'payloadUnchanged':True,'extraGpuLoad':False,'scope':'Independent ROI diagnostic; per-frame CPU waits/readback and extra base graph excluded from production'},ensure_ascii=False,indent=2),encoding='utf-8')
print('LETTERBOX_NATIVE_COMPLETE',len(results),flush=True)
