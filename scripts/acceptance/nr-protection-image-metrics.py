"""SDR screenshot measurements; color change is not a subjective quality score."""
from pathlib import Path
import json,sys
import numpy as np
from PIL import Image
folder=Path(sys.argv[1]);output=Path(sys.argv[2])
BASE=Path('E:/项目/Veyra')
assert folder.resolve().is_relative_to(BASE.resolve()) and output.resolve().is_relative_to(BASE.resolve())
def lab(path):
    rgb=np.asarray(Image.open(path).convert('RGB'),dtype=np.float64)/255
    linear=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
    lms=linear@np.array([[.4122214708,.2119034982,.0883024619],[.5363325363,.6806995451,.2817188376],[.0514459929,.1073969566,.6299787005]])
    return np.cbrt(lms)@np.array([[.2104542553,1.9779984951,.0259040371],[.7936177850,-2.4285922050,.7827717662],[-.0040720468,.4505937099,-.8086757660]])
source=lab(folder/'source.png');o=source[...,0];c=np.linalg.norm(source[...,1:],axis=-1)
neutral=(c/np.maximum(o,.03)<.08)&(o>.08)&(o<.96)
colored=(c/np.maximum(o,.03)>=.08)&(o>.08)&(o<.96)
stats={}
for path in sorted(folder.glob('*.png')):
    if path.stem=='source':continue
    frame=lab(path);assert frame.shape==source.shape
    # Chroma is rebased to current lightness: ordinary brightening should not
    # be falsely counted as a hue/saturation change of the source RGB ratios.
    chromaError=np.linalg.norm(frame[...,1:]-source[...,1:]*(frame[...,0]/np.maximum(o,.03))[...,None],axis=-1)
    stats[path.stem]={'meanColorChange':float(chromaError.mean()),'neutralColorChange':float(chromaError[neutral].mean()),
        'coloredColorChange':float(chromaError[colored].mean()),'p95ColorChange':float(np.quantile(chromaError,.95)),
        'meanLightnessChange':float(np.abs(frame[...,0]-o).mean()),'meanLightness':float(frame[...,0].mean()),
        'shadowDarkening':float(np.maximum(o-frame[...,0],0)[o<.35].mean()),
        'lightnessEdgeEnergy':float(np.abs(np.diff(frame[...,0],axis=0)).mean()+np.abs(np.diff(frame[...,0],axis=1)).mean())}
result={'source':str(folder/'source.png'),'neutralPixels':int(neutral.sum()),'coloredPixels':int(colored.sum()),'results':stats,
    'interpretation':'Source-relative color/brightness changes on one SDR frame; not a subjective quality or flicker score.'}
output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
