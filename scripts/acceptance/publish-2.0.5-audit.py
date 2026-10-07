"""Independent complete payload hashes, vendor boundaries and PE import closure."""
import hashlib, json, os, sys
from pathlib import Path
import pefile

BASE=Path('E:/项目/Veyra'); TASK='publish-2.0.5-20261007'
app=Path(sys.argv[1]).resolve(); flavor=sys.argv[2]; label=sys.argv[3]
assert app.is_relative_to(BASE.resolve()) and flavor in ('NVIDIA','AMD')
out=BASE/'logs'/TASK/(label+'.json'); assert not out.exists()
manifest=json.loads((app/'package-manifest.json').read_text(encoding='utf8'))
names={f.relative_to(app).as_posix():f for f in app.rglob('*') if f.is_file()}
expected={r['path'] for r in manifest['files']}|{'package-manifest.json'}
assert set(names)==expected, {'unexpected':list(set(names)-expected),'missing':list(expected-set(names))}
for row in manifest['files']:
    f=names[row['path']]
    with f.open('rb') as stream: sha=hashlib.file_digest(stream,'sha256').hexdigest()
    assert f.stat().st_size==row['size'] and sha==row['sha256'],row['path']
assert not any(f.suffix.lower() in ('.pdb','.lib','.pyd','.whl','.addon64') for f in names.values())
drivers={'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}
assert not drivers & {f.name.lower() for f in names.values()}
if flavor=='NVIDIA':
    assert not any(n.startswith('runtime/amd-nr/') for n in names)
    vfg={f.name for n,f in names.items() if n.startswith('runtime/nvidia-vfg/')}
    assert vfg=={'NVVideoEffects.dll','nvVFXVideoFrameGeneration.dll','NVCVImage.dll','cudart64_12.dll','nvngxruntime.dll'},vfg
else:
    assert not any(f.name.lower().startswith(('nvngx','nvvideoeffects','cudart','npp','nvcv')) for f in names.values())
    assert 'runtime/amd-nr/LmxxfNrRuntime.dll' in names
fsr={f.name for n,f in names.items() if n.startswith('runtime_local/amd/fidelityfx/')}
assert fsr=={'amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_upscaler_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll'}
runtime=json.loads((app/'release-runtime-manifest.json').read_text(encoding='utf8'))
assert runtime==json.loads((app/'runtime/experimental/release-runtime-manifest.json').read_text(encoding='utf8'))
assert {n for n,f in names.items() if n.startswith(('runtime/','runtime_local/')) and f.suffix.lower() not in ('.json','.md','.txt')} <= {r['path'] for r in runtime['files']}
packaged={f.name.lower() for f in names.values() if f.suffix.lower()=='.dll'}
system=Path(os.environ['WINDIR'])/'System32'; imports=[]; missing=[]
for name,f in names.items():
    if f.suffix.lower() not in ('.dll','.exe'):continue
    pe=pefile.PE(str(f),fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
    direct=[i.dll.decode().lower() for i in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])]
    delay=[i.dll.decode().lower() for i in getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[])]
    pe.close()
    assert not any(n.startswith('npp') for n in direct+delay),(name,direct,delay)
    for dll in direct+delay:
        if dll in packaged or dll.startswith(('api-ms-','ext-ms-')) or (system/dll).is_file():continue
        missing.append({'file':name,'dependency':dll,'delay':dll in delay})
    imports.append({'file':name,'direct':direct,'delay':delay})
assert not missing,missing
result=dict(flavor=flavor,app=str(app),payloadFiles=len(manifest['files']),payloadBytes=sum(r['size'] for r in manifest['files']),
            sourceCommit=manifest.get('baseCommit'),releaseReady=manifest.get('releaseReady'),runtimeFiles=len(runtime['files']),
            peFiles=len(imports),imports=imports,missing=missing,fsrException=True)
out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('PACKAGE AUDIT PASS',flavor,result['payloadFiles'],result['payloadBytes'],out,flush=True)
