"""Audit the extracted local candidate, then start it with Windows-only PATH.

The checks concern package closure and unchanged dependency bytes. They do not
replace the separately recorded NR/FG/worker or hardware acceptance runs.
"""
from pathlib import Path
import hashlib, importlib.util, json, os, subprocess, sys, zipfile
import pefile

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
app=Path(sys.argv[1]).resolve();label=sys.argv[2]
assert app.is_relative_to((BASE/'verify'/TASK).resolve()) and label.replace('-','').isalnum()
matrix.assert_gpu_tests_idle()
out=BASE/'logs'/TASK/label;out.mkdir(exist_ok=False)
manifest=json.loads((app/'package-manifest.json').read_text(encoding='utf-8'))
assert manifest['localOnly'] and not manifest['releaseReady'] and manifest['gpuPackage']=='NVIDIA'
files={p.relative_to(app).as_posix():p for p in app.rglob('*') if p.is_file()}
assert set(files)=={row['path'] for row in manifest['files']}|{'package-manifest.json'}
for row in manifest['files']:
    path=files[row['path']]
    assert path.stat().st_size==row['size'] and matrix.digest(path)==row['sha256'],row['path']
assert not any(p.suffix.lower() in ('.pdb','.lib','.pyd','.whl','.addon64') for p in files.values())
assert not any(p.name.endswith('_tests.exe') for p in files.values())
assert not any(name.startswith(('runtime/amd-nr/','runtime_local/amd/nr/')) for name in files)
drivers={'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}
assert not drivers & {p.name.lower() for p in files.values()}
fsr={p.name for name,p in files.items() if name.startswith('runtime_local/amd/fidelityfx/')}
assert fsr=={'amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_upscaler_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll'}
vfg={p.name for name,p in files.items() if name.startswith('runtime/nvidia-vfg/')}
assert vfg=={'NVVideoEffects.dll','nvVFXVideoFrameGeneration.dll','NVCVImage.dll','cudart64_12.dll','nvngxruntime.dll'}
baseline=BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
runtime=json.loads((app/'release-runtime-manifest.json').read_text(encoding='utf-8'))
assert runtime==json.loads((baseline/'release-runtime-manifest.json').read_text(encoding='utf-8'))
assert runtime==json.loads((app/'runtime/experimental/release-runtime-manifest.json').read_text(encoding='utf-8'))
immutable={'.dll','.hsaco','.ptx','.onnx','.f16','.bin','.cubin'}
dependencies=[]
for name,path in files.items():
    if path.suffix.lower() in immutable:
        original=baseline/name
        assert original.is_file() and matrix.digest(path)==matrix.digest(original),name
        dependencies.append(name)
assert matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll')=='f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc'
packaged={p.name.lower() for p in files.values() if p.suffix.lower()=='.dll'}
system=Path(os.environ['WINDIR'])/'System32';imports=[];missing=[]
for name,path in files.items():
    if path.suffix.lower() not in ('.exe','.dll'):continue
    pe=pefile.PE(str(path),fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
    direct=[row.dll.decode().lower() for row in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])]
    delay=[row.dll.decode().lower() for row in getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[])]
    pe.close();assert not any(dll.startswith('npp') for dll in direct+delay),(name,direct,delay)
    for dll in direct+delay:
        if dll in packaged or dll.startswith(('api-ms-','ext-ms-')) or (system/dll).is_file():continue
        missing.append({'file':name,'dependency':dll,'delay':dll in delay})
    imports.append({'file':name,'direct':direct,'delay':delay})
assert not missing,missing
(out/'imports.json').write_text(json.dumps(imports,ensure_ascii=False,indent=2),encoding='utf-8')
source_zip=BASE/'test-packages'/TASK/Path(manifest['correspondingSource']['application']).name
assert matrix.digest(source_zip)==manifest['sourceZipSha256']
with zipfile.ZipFile(source_zip) as source:
    assert not any(Path(name).suffix.lower() in immutable|{'.lib','.pdb','.addon64'} for name in source.namelist())
    assert source.read('qml/Veyra/Main.qml')==(ROOT/'qml/Veyra/Main.qml').read_bytes()
    assert source.read('src/engine/VideoPresenter.cpp')==subprocess.check_output(['git','-C',str(ROOT),'show',manifest['sourceArchiveCommit']+':src/engine/VideoPresenter.cpp'])
runs=[]
for mode in ('gpu','obs-compat'):
    matrix.assert_gpu_tests_idle()
    tmp=BASE/'tmp'/TASK/(label+'-'+mode);tmp.mkdir(exist_ok=False)
    profile=BASE/'tests'/TASK/(label+'-'+mode);profile.mkdir(exist_ok=False)
    (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','prewarmEnhancement':False}),encoding='utf-8')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
    log=out/(mode+'-player.log')
    env.update(TEMP=str(tmp),TMP=str(tmp),PATH=os.environ['WINDIR']+'/System32;'+os.environ['WINDIR'],
               VEYRA_LOG_FILE=str(log),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
    args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','7000']
    if mode=='obs-compat':args.append('--obs-game-capture')
    args.append(str(matrix.SOURCES['M1']))
    with (out/(mode+'-console.log')).open('xb') as stream:
        rc=subprocess.run(args,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=40).returncode
    text=log.read_text(encoding='utf-8',errors='replace')
    assert rc==0 and 'Veyra QML session started' in text and 'playback' in text.lower(),(mode,rc)
    assert not any(value in text for value in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object'))
    if mode=='obs-compat':assert 'OBS game capture compatibility: software UI' in text
    assert all(matrix.digest(app/row['path'])==row['sha256'] for row in manifest['files'])
    assert {p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()}==set(files)
    runs.append({'mode':mode,'command':args,'exitCode':rc,'passed':True,'windowsOnlyPath':True})
    print('FINAL_PACKAGE_START_PASS',mode,flush=True)
result={'app':str(app),'payloadFiles':len(manifest['files']),'peFiles':len(imports),'runtimeFiles':len(runtime['files']),
        'unchangedDependencyFiles':len(dependencies),'importsMissing':missing,'sourceSha256':matrix.digest(source_zip),
        'runs':runs,'passed':True,'localOnly':True,'newHardwareAcceptance':False}
(out/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('FINAL_PACKAGE_AUDIT_PASS',len(manifest['files']),len(imports),flush=True)
