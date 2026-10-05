"""Audit same-input native captures; do not confuse color distance with quality."""
from pathlib import Path
import hashlib,json
BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005';LOG=BASE/'logs'/TASK
def read(label,file):return json.loads((LOG/('styles-'+label)/file).read_text(encoding='utf8'))
old=read('original-baseline3','result.json');new=read('original-fixed3','result.json')
oldImages={c['label']:c['sha256'] for c in old['captures']};newImages={c['label']:c['sha256'] for c in new['captures']}
assert old['passed'] and new['passed'] and oldImages['source']==newImages['source']
assert old['media']==new['media'] and new['media'].lower().endswith('.png')
for style in range(3):assert oldImages[f'style{style}-raw']==newImages[f'style{style}-raw']
raw=newImages['style1-raw']
assert raw==newImages['style1-manual-none']==newImages['style1-auto-zero']
for key in ('neutralProtection','colorRetention','luminanceRetention','shadowProtection'):
    assert newImages['style1-manual-'+key]!=raw
a=read('original-baseline3','image-metrics.json')['results'];b=read('original-fixed3','image-metrics.json')['results']
for style in (1,2):assert b[f'style{style}-auto']['meanColorChange']<a[f'style{style}-auto']['meanColorChange']*.2
assert b['style0-auto']['neutralColorChange']<a['style0-auto']['neutralColorChange']*.5
assert b['style1-manual-neutralProtection']['neutralColorChange']<b['style1-manual-none']['neutralColorChange']*.5
assert b['style1-manual-colorRetention']['meanColorChange']<b['style1-raw']['meanColorChange']*.15
assert b['style1-manual-colorRetention']['meanLightnessChange']>b['style1-raw']['meanLightnessChange']*.85
assert b['style1-manual-luminanceRetention']['meanLightnessChange']<b['style1-raw']['meanLightnessChange']*.1
assert b['style1-manual-luminanceRetention']['meanColorChange']>b['style1-raw']['meanColorChange']*.6
assert b['style1-manual-shadowProtection']['shadowDarkening']<b['style1-raw']['shadowDarkening']*.4
runtime=BASE/'test-packages'/TASK/'Veyra-2.0.3-nr-controls2-NVIDIA-win64-portable/runtime/experimental/nr-original/nvngx_dlssnr.dll'
assert hashlib.sha256(runtime.read_bytes()).hexdigest().upper()=='E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'
result={'passed':True,'sourceSha256':oldImages['source'],'exeSha256':new['exeSha256'],
 'allThreeRawStylesByteIdentical':True,'zeroManualAndZeroAutoByteIdentical':True,'newManualControlsIndependent':True,
 'autoColorChanges':{str(s):{'before':a[f'style{s}-auto']['meanColorChange'],'after':b[f'style{s}-auto']['meanColorChange']} for s in range(3)},
 'interpretation':'One fixed SDR input on RTX 5070/NVIDIA original. Source-relative color distance; not a subjective quality, flicker or all-content score.'}
path=LOG/'style-picture-comparison.json'
with path.open('x',encoding='utf8') as out:json.dump(result,out,ensure_ascii=False,indent=2)
print(json.dumps(result,ensure_ascii=False,indent=2))
