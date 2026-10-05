"""CPU reference of the proposed thresholds/confirmation, diagnostic only.
Run after GPU timing tests; authored truth exposes false crops and late text.
"""
from pathlib import Path
import json,sys
import numpy as np
from PIL import Image
BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004';label=sys.argv[1]
out=BASE/'tests'/TASK/label;out.mkdir(parents=True,exist_ok=False)
W,H=1920,1080
# sRGB -> linear; exact authored pixels, no codec/tone-mapping ambiguity.
table=np.arange(256,dtype=np.float64)/255
table=np.where(table<=.04045,table/12.92,((table+.055)/1.055)**2.4)
rects=[(0,138,1920,804),(0,132,1920,816),(0,104,1920,872),(0,22,1920,1036),(240,0,1440,1080),(150,0,1620,1080)]
def detect(a):
 rgb=table[a[:,:,:3]];peak=rgb.max(axis=2)
 if float(np.mean(peak>.03))<.1:return 'dark'
 def leading(values):
  bad=np.flatnonzero(~values);return int(bad[0]) if len(bad) else len(values)
 rows=peak.max(axis=1)<.015;cols=peak.max(axis=0)<.015
 top,bottom=leading(rows),leading(rows[::-1]);left,right=leading(cols),leading(cols[::-1])
 for x,y,w,h in rects:
  # Snap measured symmetric borders, not any smaller rectangle whose outer
  # strips happen to be black. That would change ratio when text appears.
  if y and (abs(top-y)>2 or abs(bottom-y)>2):continue
  if x and (abs(left-x)>2 or abs(right-x)>2):continue
  strips=[]
  if y:strips.extend((peak[:y,:],peak[y+h:,:]))
  if x:strips.extend((peak[:,:x],peak[:,x+w:]))
  if all(float(s.max())<.015 and float(s.var())<.00001 for s in strips):return (x,y,w,h)
 return None
class Confirm:
 def __init__(self):self.active=None;self.candidate=None;self.frames=0
 def update(self,value):
  if value=='dark':return self.active
  if value!=self.candidate:self.candidate=value;self.frames=0
  self.frames+=1
  if self.frames>=60:self.active=value
  return self.active
def canvas(margins=0):
 a=np.full((H,W,4),255,dtype=np.uint8);a[:,:,:3]=margins
 a[138:942,:,:3]=110
 return a
rows=[]
for name,a,truth,initial in [('standard239',canvas(),(0,138,W,804),False),
 ('full-frame-dark-content-at-edges',canvas(18),(0,0,W,H),False),
 ('black-fade-holds-confirmed-roi',np.zeros((H,W,4),dtype=np.uint8),(0,138,W,804),True),
 ('late-burned-in-subtitle',canvas(),(0,0,W,H),True)]:
 if name=='late-burned-in-subtitle':a[992:1014,840:960,:3]=235
 a[:,:,3]=255
 candidate=detect(a);state=Confirm()
 if initial:
  for _ in range(60):state.update((0,138,W,804))
 frames=[state.update(candidate) for _ in range(75)]
 false=[i for i,r in enumerate(frames) if r is not None and tuple(r)!=tuple(truth)]
 removed=[]
 for r in frames:
  if r is None:removed.append(0);continue
  x,y,w,h=r;mask=np.ones((H,W),dtype=bool);mask[y:y+h,x:x+w]=False
  removed.append(int(np.count_nonzero(np.any(a[:,:,:3]!=0,axis=2)&mask)))
 row={'case':name,'truth':truth,'detected':candidate,'falseCropFrames':false,
      'maxNonblackPixelsErasedByFillBlack':max(removed),'observations':frames,'authored':True}
 rows.append(row);Image.fromarray(a).save(out/(name+'.png'))
(out/'results.json').write_text(json.dumps({'cases':rows,'linearThreshold':.015,'confirmationFrames':60,
 'scope':'CPU diagnostic reference, no normal-playback readback/scanning installed',
 'verdict':'Proposed fill-black and approximate-black detection have concrete content-loss counterexamples'},ensure_ascii=False,indent=2),encoding='utf-8')
assert not rows[0]['falseCropFrames'] and not rows[2]['falseCropFrames']
assert rows[1]['falseCropFrames'] and rows[1]['maxNonblackPixelsErasedByFillBlack']>500000
assert len(rows[3]['falseCropFrames'])==59 and rows[3]['maxNonblackPixelsErasedByFillBlack']==2640
print('LETTERBOX_DETECTION_COUNTEREXAMPLES',json.dumps([{k:r[k] for k in ('case','detected','falseCropFrames','maxNonblackPixelsErasedByFillBlack')} for r in rows],ensure_ascii=False))
