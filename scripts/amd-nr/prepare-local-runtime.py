"""Audit and stage user-provided 0.39 assets, separately from source Git.

No Magpie executable, proxy dxgi.dll or ReShade add-on is copied. The model
weights are NVIDIA-derived assets and are not covered by the code's MIT license.
"""
import argparse, collections, ctypes, hashlib, json, shutil, subprocess
from pathlib import Path

PIN='78f548749e74824327b8458c57be31a1df78376a'
p=argparse.ArgumentParser()
p.add_argument('--package',type=Path,required=True,help='Extracted user package')
p.add_argument('--source',type=Path,required=True)
p.add_argument('--dll',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
a.out.resolve().relative_to(Path('E:/项目/Veyra').resolve())
if a.out.exists():raise SystemExit('Output must be a new directory')
if subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.source,text=True).strip()!=PIN:
    raise SystemExit('Unexpected upstream source revision')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
count=0
for line in (a.package/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
    digest,name=line.split(None,1);file=(a.package/name.lstrip('*')).resolve()
    file.relative_to(a.package.resolve())
    if not file.is_file() or sha(file)!=digest.lower():raise SystemExit('Package checksum mismatch: '+name)
    count+=1
assets=a.package/'DLSS5-AMD/native-game-tiled-assets'
extensions=collections.Counter(f.suffix for f in assets.rglob('*') if f.is_file())
if extensions['.f16']!=122 or extensions['.f32']!=64 or extensions['.hsaco']!=62:
    raise SystemExit('Expected the reviewed 0.39 weight/kernel set')
a.out.mkdir(parents=True)
shutil.copytree(assets,a.out/'assets')
shutil.copy2(a.dll,a.out/'LmxxfNrRuntime.dll')
shutil.copy2(a.source/'LICENSE',a.out/'LICENSE-lmxxf-MIT.txt')
shutil.copy2(a.package/'HIP-API-LICENSE.txt',a.out/'HIP-API-LICENSE.txt')
# The independent C ABI uses its own current linear encode/decode shaders.
# Overlay the pinned source shader files, preserving unchanged binary weights.
for file in (a.source/'shaders').glob('*.hlsl'):
    shutil.copy2(file,a.out/'assets'/file.name)
hip=a.out/'assets/HIP'
kernel_sums=''.join(sha(f)+'  '+f.relative_to(hip).as_posix()+'\n' for f in sorted(hip.rglob('*.hsaco')))
(hip/'SHA256SUMS').write_text(kernel_sums,encoding='ascii')
# Preserve the supplied kernel choices and skip policy. Add-on display/codec
# settings are intentionally not copied to the independent runtime.
keys=[]
for line in (a.package/'DLSS5-AMD/native-game-flags.txt').read_text(encoding='utf-8-sig').splitlines():
    if line.startswith('DLSS5_HIP_') or line.startswith(('DLSS5_SKIP_BLOCKS=','DLSS5_NETWORK_HEIGHT=','DLSS5_NETWORK_1080_ROWS=','DLSS5_STYLE=','DLSS5_DIRECT_IO=')):
        keys.append(line)
keys+=['DLSS5_FAST_NUMERIC=0','DLSS5_FRAME_STATS=0']
(a.out/'assets/native-game-flags.txt').write_text('\n'.join(keys)+'\n',encoding='ascii')
(a.out/'LOCAL-EXPERIMENT.txt').write_text('Veyra AMD NR local experiment\nRuntime code: lmxxf MIT, '+PIN+'\nAssets: user-provided Magpie-DLSS5-AMD 0.39 package. NVIDIA-derived model weights are not MIT code.\nRequires AMD RX 9000 and driver-provided amdhip64_7.dll; do not copy a driver from another machine.\nNo ReShade/Magpie injection module is loaded by Veyra. AMD inference/quality not verified on this RTX 5070.\nInternal input: height <=1080, <=1920x1080 pixels; native export above this budget is rejected.\nHDR is rejected. Keep process DLSS5_CODEC_SRGB/DLSS5_VIT_ADAPTIVE absent or 0.\n',encoding='utf8')
files=[{'path':f.relative_to(a.out).as_posix(),'size':f.stat().st_size,'sha256':sha(f)} for f in sorted(a.out.rglob('*')) if f.is_file()]
report={'upstream':PIN,'asset_package':str(a.package.resolve()),'package_checksums':count,
        'weight_files':186,'mapping_files':2,'hip_modules':62,'architectures':['gfx1200','gfx1201'],
        'inference_verified':False,'files':files}
(a.out/'amd-nr-local-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
# Read the real runtime's ABI and validate the supplied module layout without
# attempting HIP inference or supplying fake output as a product fallback.
class Api(ctypes.Structure):
    _fields_=[('size',ctypes.c_uint32),('abi',ctypes.c_uint32)]+[(n,ctypes.c_void_p) for n in ['QueryCapabilities','Create','Destroy','PrepareSession','PrepareFrame','RecordInputs','EnqueueHip','RecordOutputs','ExecuteAfterProducer','CancelUnsubmitted','Poll','Retire','ResetHistory','Drain','GetStatus','GetLastError','GetTimings']]
class Caps(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ['size','abi','width','height','history','overlap','graph','hip','gfx1201']]
class CreateInfo(ctypes.Structure):
    _fields_=[('size',ctypes.c_uint32),('device',ctypes.c_void_p),('queue',ctypes.c_void_p),('assets',ctypes.c_wchar_p),('flags',ctypes.c_uint32)]
dll=ctypes.WinDLL(str((a.out/'LmxxfNrRuntime.dll').resolve()),winmode=0x1100)
api=Api();api.size=ctypes.sizeof(api)
get=dll.LmxxfNrGetApi;get.argtypes=[ctypes.c_uint32,ctypes.POINTER(Api)];get.restype=ctypes.c_int32
assert get(1,ctypes.byref(api))==0 and api.abi==1
caps=Caps();caps.size=ctypes.sizeof(caps)
assert ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.POINTER(Caps))(api.QueryCapabilities)(ctypes.byref(caps))==0
info=CreateInfo();info.size=ctypes.sizeof(info);info.device=info.queue=1;info.assets=str((a.out/'assets').resolve())
ctx=ctypes.c_void_p()
status=ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.POINTER(CreateInfo),ctypes.POINTER(ctypes.c_void_p))(api.Create)(ctypes.byref(info),ctypes.byref(ctx))
assert status==0 and ctx.value,'Real runtime rejected the audited module layout'
assert ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.c_void_p)(api.Destroy)(ctx)==0
print('AMD RESOURCE PASS',count,'package hashes;',186,'weights;',62,'HIP modules; real ABI/layout admitted. No AMD inference test.',a.out)
