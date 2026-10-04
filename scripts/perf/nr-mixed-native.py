"""Full-frame determinism/seek proof and inspectable existing-size tradeoffs."""
from pathlib import Path
import csv,importlib.util,json,os,shutil,subprocess,sys,time,math
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
app=matrix.stage(variant,label);exe=app/'veyra_nr_mixed_resolution_experiment.exe'
shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
identity={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8')
results=[];frameGroups={}
for group in ('none','2-native','2-mixed','3-native','3-mixed'):
 for repeat in range(1,(2 if group=='none' else 3)):
  name=f'{label}-{group}-r{repeat}';out=BASE/'tests'/TASK/name;out.mkdir(parents=True,exist_ok=False)
  tmp=BASE/'tmp'/TASK/name;tmp.mkdir(parents=True,exist_ok=False)
  env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
  env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1');matrix.assert_gpu_tests_idle()
  print('START',name,flush=True);start=time.monotonic()
  with (out/'console.log').open('x',encoding='utf-8') as stream:
   proc=subprocess.run([str(exe),str(matrix.SOURCES['M1']),str(out),group],cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250)
  console=(out/'console.log').read_text(encoding='utf-8',errors='replace')
  log=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
  passValue=proc.returncode==0 and 'NR_MIXED_RESULT pass=1 frames=40 debugErrors=0 deviceRemoved=0' in console and '[ERROR]' not in log
  receipt={'name':name,'group':group,'repeat':repeat,'passed':passValue,'exitCode':proc.returncode,'wallSeconds':time.monotonic()-start,
           'exeSha256':matrix.digest(exe),'runtimeSha256':identity['runtime/experimental/nvngx_dlssnr.dll'],'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
           'sourceCommit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip(),'plan':next((l for l in console.splitlines() if l.startswith('NR_MIXED_PLAN ')),None)}
  (out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8');results.append(receipt)
  (folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');assert passValue,receipt
  with (out/'frames.csv').open(encoding='utf-8') as stream:frames=list(csv.DictReader(stream))
  assert len(frames)==40 and {int(f['epoch']) for f in frames}=={1,2} and int(frames[20]['pts100ns'])>=100000000
  if group in frameGroups:assert frames==frameGroups[group],'Same configuration re-run differs'
  else:frameGroups[group]=frames
quality=BASE/'tests'/TASK/(label+'-quality');quality.mkdir(parents=True,exist_ok=False)
comparisons=[];html=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>4b逐层尺寸画面对照</title><style>body{background:#18191c;color:#eee;font-family:system-ui;margin:24px}.pair{display:flex;gap:12px}figure{margin:0;flex:1}img{width:100%}p{color:#bbc1ca}a{color:#9abaff}</style><h1>既有逐层尺寸 · 画面对照</h1><p>左：每层1080p；右：先降低前层，末层1080p。不同配置会改变画面。PSNR只描述差异，不能证明画质更好。所有图来自同一产品实际完整输出；未获用户画质确认。点击打开全尺寸PNG。</p>']
for count in (2,3):
 rows=[];sheet=Image.new('RGB',(960,4*292),(24,25,28));draw=ImageDraw.Draw(sheet)
 for index,frame in enumerate((0,1,19,20,21,39)):
  paths=[BASE/'tests'/TASK/f'{label}-{count}-{mode}-r1'/f'frame-{frame}.png' for mode in ('native','mixed')]
  images=[np.asarray(Image.open(p).convert('RGBA'),dtype=np.int16) for p in paths];assert images[0].shape==images[1].shape==(1080,1920,4)
  diff=images[1]-images[0];assert np.count_nonzero(diff[:,:,3])==0
  mse=float(np.mean(diff[:,:,:3].astype(np.float64)**2));row={'frame':frame,'changedRgbPixelsPercent':float(np.any(diff[:,:,:3]!=0,axis=2).mean()*100),
        'maxRgb8':int(np.abs(diff[:,:,:3]).max()),'psnrRgb8Db':10*math.log10(255**2/mse) if mse else None,'alphaDifferentPixels':0}
  psnrLabel=f'{row["psnrRgb8Db"]:.2f}dB' if mse else '完全一致'
  rows.append(row);html.append(f'<h2>{count}层 · 输出帧{frame}</h2><p>变化像素 {row["changedRgbPixelsPercent"]:.2f}% · PSNR {psnrLabel} · 最大RGB8差 {row["maxRgb8"]}</p><div class="pair">')
  for p,caption in zip(paths,('所有层1080p','前层降低，末层1080p')):html.append(f'<figure><figcaption>{caption}</figcaption><a href="{p.as_uri()}"><img src="{p.as_uri()}"></a></figure>')
  html.append('</div>')
  if frame in (0,19,20,39):
   sheetIndex=(0,19,20,39).index(frame);draw.text((4,sheetIndex*292+4),f'{count} layers / frame {frame}: full 1080 | mixed',fill='white')
   for x,p in enumerate(paths):sheet.paste(Image.open(p).convert('RGB').resize((480,270)),(x*480,sheetIndex*292+22))
 sheet.save(quality/f'{count}-comparison.png');comparisons.append({'layers':count,'frames':rows})
html.append('</html>');(quality/'review.html').write_text('\n'.join(html),encoding='utf-8')
assert all(matrix.digest(app/name)==sha for name,sha in identity.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'results':results,'completeRgbaFrames':360,'sameConfigurationDifferences':0,'seekPairs':8,'comparisons':comparisons,
 'payloadUnchanged':True,'humanQualityApproval':False,'diagnosticReadbackOnly':True},ensure_ascii=False,indent=2),encoding='utf-8')
print('MIXED_NATIVE_COMPLETE',len(results),flush=True)
