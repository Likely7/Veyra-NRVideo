"""Real AMD ABI/layout validation only; no HIP inference on our NVIDIA card."""
import ctypes, hashlib, json, sys
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
amd=Path(sys.argv[1]) if len(sys.argv)>1 else BASE/'test-packages'/TASK/'Veyra-2.0.2-slim-20261004-win64-portable/runtime/amd-nr'
manifest=json.loads((amd/'amd-nr-local-manifest.json').read_text(encoding='utf8'))
assert manifest['upstream']=='78f548749e74824327b8458c57be31a1df78376a'
for row in manifest['files']:
    p=amd/row['path'];assert p.stat().st_size==row['size'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'],p
weights=[p for p in (amd/'assets').rglob('*') if p.suffix in ('.f16','.f32')]
kernels=list((amd/'assets/HIP').rglob('*.hsaco'));assert len(weights)==186 and len(kernels)==62
class Api(ctypes.Structure):
    _fields_=[('size',ctypes.c_uint32),('abi',ctypes.c_uint32)]+[(n,ctypes.c_void_p) for n in ['QueryCapabilities','Create','Destroy','PrepareSession','PrepareFrame','RecordInputs','EnqueueHip','RecordOutputs','ExecuteAfterProducer','CancelUnsubmitted','Poll','Retire','ResetHistory','Drain','GetStatus','GetLastError','GetTimings']]
class Caps(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ['size','abi','width','height','history','overlap','graph','hip','gfx1201']]
class CreateInfo(ctypes.Structure):
    _fields_=[('size',ctypes.c_uint32),('device',ctypes.c_void_p),('queue',ctypes.c_void_p),('assets',ctypes.c_wchar_p),('flags',ctypes.c_uint32)]
dll=ctypes.WinDLL(str((amd/'LmxxfNrRuntime.dll').resolve()),winmode=0x1100)
api=Api();api.size=ctypes.sizeof(api);get=dll.LmxxfNrGetApi;get.argtypes=[ctypes.c_uint32,ctypes.POINTER(Api)];get.restype=ctypes.c_int32
assert get(1,ctypes.byref(api))==0 and api.abi==1
caps=Caps();caps.size=ctypes.sizeof(caps);assert ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.POINTER(Caps))(api.QueryCapabilities)(ctypes.byref(caps))==0
info=CreateInfo();info.size=ctypes.sizeof(info)
# The upstream Create function validates assets but does not dereference the
# device/queue until PrepareSession. Do not call PrepareSession or execute HIP.
info.device=info.queue=1;info.assets=str((amd/'assets').resolve());ctx=ctypes.c_void_p()
assert ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.POINTER(CreateInfo),ctypes.POINTER(ctypes.c_void_p))(api.Create)(ctypes.byref(info),ctypes.byref(ctx))==0 and ctx.value
assert ctypes.CFUNCTYPE(ctypes.c_int32,ctypes.c_void_p)(api.Destroy)(ctx)==0
result={'filesVerified':len(manifest['files']),'weights':len(weights),'hipModules':len(kernels),'apiBytes':api.size,'abi':api.abi,'caps':{n:getattr(caps,n) for n,_ in caps._fields_},'moduleLayoutPassed':True,'inferenceVerified':False,'runtime':str(amd)}
(BASE/'logs'/TASK/'amd-runtime-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('AMD ABI/LAYOUT PASS',json.dumps(result,ensure_ascii=False))
