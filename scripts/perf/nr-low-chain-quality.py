"""Full-frame RGBA identity and numerical changes; no automatic quality approval."""
from pathlib import Path
import csv,hashlib,json,math,statistics,sys
import numpy as np
from PIL import Image
BASE=Path('E:/项目/Veyra/logs/perf-nr-20261004');label=sys.argv[1];assert label.replace('-','').isalnum()
source=json.loads((BASE/(label+'-summary.json')).read_text(encoding='utf-8'));comparisons=[]
for group in [c['group'] for c in source['comparisons']]:
 off,on=[r for r in source['results'] if r['group']==group];rows=[]
 for ref,test in zip(off['frames'],on['frames']):
  assert all(ref[k]==test[k] for k in ('frame','pts100ns','width','height','nrDelta'))
  images=[]
  for run,row in ((off,ref),(on,test)):
   with Image.open(BASE/run['name']/('frame-'+row['frame']+'.png')) as image:rgba=np.array(image.convert('RGBA'),dtype=np.uint8)
   assert hashlib.sha256(rgba.tobytes()).hexdigest()==row['sha256'].lower(),'PNG changed native RGBA pixels';images.append(rgba)
  a,b=images;delta=np.abs(a[:,:,:3].astype(np.float32)-b[:,:,:3].astype(np.float32));mse=float(np.mean(delta*delta));changed=np.max(delta,axis=2)>0
  protected_differences=0
  if group=='protected':
   height,width=changed.shape;protected_differences=int(np.count_nonzero(changed[int(height*.25):int(height*.75),int(width*.25):int(width*.75)]));assert protected_differences==0,'Protected interior changed'
  rows.append({'frame':int(ref['frame']),'changedRgbPixelsPercent':float(np.mean(changed)*100),'meanAbsoluteRgb8':float(np.mean(delta)),'rmseRgb8':math.sqrt(mse),'psnrRgb8Db':10*math.log10(255*255/mse) if mse else None,'maxRgb8':float(delta.max()),'alphaDifferentPixels':int(np.count_nonzero(a[:,:,3]!=b[:,:,3])),'protectedInteriorDifferentPixels':protected_differences})
 active=bool(on['metrics']['active'])
 if not active:assert all(r['changedRgbPixelsPercent']==0 for r in rows)
 assert all(r['alphaDifferentPixels']==0 for r in rows),'Alpha changed'
 result={'group':group,'active':active,'frames':rows,'medianChangedRgbPixelsPercent':statistics.median(r['changedRgbPixelsPercent'] for r in rows),'medianPsnrRgb8Db':statistics.median([r['psnrRgb8Db'] for r in rows if r['psnrRgb8Db'] is not None]) if any(r['psnrRgb8Db'] is not None for r in rows) else None,'maxRgb8':max(r['maxRgb8'] for r in rows),'alphaDifferentPixels':0}
 comparisons.append(result);print('LOW_CHAIN_PIXELS',json.dumps({k:v for k,v in result.items() if k!='frames'}),flush=True)
folder=BASE/(label+'-quality');folder.mkdir(parents=True,exist_ok=False)
(folder/'comparison.json').write_text(json.dumps({'comparisons':comparisons,'measurement':'Difference from former per-layer full-resolution composites, not ground-truth fidelity or human approval; full PNG/native SHA and alpha verified'},ensure_ascii=False,indent=2),encoding='utf-8')
html=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>4a完整输出对照</title><style>body{background:#18191c;color:#eee;font-family:system-ui;margin:32px}article{margin:32px 0}.pair{display:flex;gap:12px}figure{margin:0;flex:1}img{width:100%;cursor:zoom-in}p{color:#bbc1ca}a{color:#9abaff}</style><h1>多层NR低分辨率链 · 完整输出对照</h1><p>左：原每层全尺寸叠回；右：低分辨率逐层保留残差，出口一次叠回。点击打开完整PNG。PSNR只描述变化，不能证明画质更好。默认关闭，未获人工画质确认。</p>']
for comp in comparisons:
 if not comp['active']:continue
 off,on=[r for r in source['results'] if r['group']==comp['group']]
 for frame in (0,10,20,39):
  row=comp['frames'][frame];psnr=f'{row["psnrRgb8Db"]:.2f}dB' if row['psnrRgb8Db'] is not None else '完全一致'
  html.append(f'<article><h2>{comp["group"]} · 源帧{frame}</h2><p>变化像素{row["changedRgbPixelsPercent"]:.2f}% · PSNR {psnr} · 最大RGB8差{row["maxRgb8"]:.0f}</p><div class="pair">')
  for run,caption in ((off,'原链'),(on,'低分辨率链')):
   uri=(BASE/run['name']/f'frame-{frame}.png').as_uri();html.append(f'<figure><figcaption>{caption}</figcaption><a href="{uri}"><img src="{uri}"></a></figure>')
  html.append('</div></article>')
html.append('</html>');(folder/'review.html').write_text('\n'.join(html),encoding='utf-8');print('LOW_CHAIN_QUALITY_REPORT',folder,flush=True)
