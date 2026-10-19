"""Stage user-approved 0.44 assets and independently built Small/Original modules.
No game/proxy/add-on, driver or SDK is copied. This audit is not HIP validation.
"""
import argparse, collections, ctypes, hashlib, json, shutil, subprocess, zipfile
from pathlib import Path
PIN='4764e4f1a24f5234b8e29952dc41f493b2d77c90'
ARCHIVE_SHA='cf812fe7909fbd53b38f54da8abd584698525f81f7ea71cab19ba14bab48e030'
p=argparse.ArgumentParser()
for name in ('archive','source','dll','original-dll','out'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
a.out.resolve().relative_to(Path('E:/项目/Veyra').resolve())
if a.out.exists():raise SystemExit('Output must be a new directory')
def sha(file):
    with file.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if sha(a.archive)!=ARCHIVE_SHA:raise SystemExit('Unexpected user package identity')
if subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.source,text=True).strip()!=PIN or subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=a.source):
    raise SystemExit('Expected unchanged pinned source checkout')
with zipfile.ZipFile(a.archive) as archive:
    rows=[]
    for line in archive.read('SHA256SUMS.txt').decode('utf-8-sig').splitlines():
        digest,name=line.split(None,1);name=name.lstrip('*').replace('\\','/')
        if hashlib.sha256(archive.read(name)).hexdigest()!=digest.lower():raise SystemExit('Package checksum mismatch: '+name)
        rows.append(name)
    if len(rows)!=875:raise SystemExit('Unexpected package file count')
    release=json.loads(archive.read('release.json'))
    if release['source_commit']!=PIN or release['hotfix']!='20261018-F9':raise SystemExit('Unexpected release metadata')
    prefix='DLSS5-AMD/native-game-tiled-assets/'
    assets=[name for name in rows if name.startswith(prefix)]
    counts=collections.Counter(Path(name).suffix for name in assets)
    small=[name for name in assets if '/small/' in name and Path(name).suffix in ('.f16','.f32')]
    if counts['.hsaco']!=80 or len(small)!=93:raise SystemExit('Unexpected 0.44 weight/kernel layout')
    a.out.mkdir(parents=True)
    for name in assets:
        relative=Path(name[len(prefix):]);target=(a.out/'assets'/relative).resolve()
        target.relative_to((a.out/'assets').resolve())
        if target.suffix.lower() in ('.exe','.dll','.addon64','.lib'):raise SystemExit('Unexpected executable in assets')
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(archive.read(name))
    for name in ('HIP-API-LICENSE.txt','release.json'):(a.out/name).write_bytes(archive.read(name))
    # Preserve kernel choices, not the add-on's codec/present/history/hotkeys.
    selected=[]
    for line in archive.read('DLSS5-AMD/default-config.txt').decode('utf-8-sig').splitlines():
        if line.startswith(('DLSS5_HIP_','DLSS5_SKIP_BLOCKS=','DLSS5_NETWORK_HEIGHT=',
            'DLSS5_NETWORK_1080_ROWS=','DLSS5_NETWORK_FREE_RES=','DLSS5_FIT_LARGE=',
            'DLSS5_STYLE=','DLSS5_DIRECT_IO=')):selected.append(line)
    selected+=['DLSS5_FAST_NUMERIC=0','DLSS5_FRAME_STATS=0','DLSS5_STRENGTH=auto',
        'DLSS5_HOT_RELOAD=0','DLSS5_MULTI_PASS_HOTKEY=0','DLSS5_VIT_REUSE_HOTKEY=0',
        'DLSS5_VIT_ADAPTIVE=0','DLSS5_TEMPORAL_MODE=0','DLSS5_FAST_HISTORY=0',
        'DLSS5_TEMPORAL_HISTORY_EXPERIMENT=0']
    (a.out/'assets/native-game-flags.txt').write_text('\n'.join(selected)+'\n','ascii')
for src,name in ((a.dll,'LmxxfNrRuntime.dll'),(a.original_dll,'LmxxfNrRuntime-Original.dll')):shutil.copy2(src,a.out/name)
shutil.copy2(a.source/'LICENSE',a.out/'LICENSE-lmxxf-MIT.txt')
for file in (a.source/'shaders').glob('*.hlsl'):shutil.copy2(file,a.out/'assets'/file.name)
hip=a.out/'assets/HIP'
(hip/'SHA256SUMS').write_text(''.join(sha(f)+'  '+f.relative_to(hip).as_posix()+'\n' for f in sorted(hip.rglob('*.hsaco'))),'ascii')
# Both real modules stay loaded simultaneously. Test the compiled selectors and
# upstream's own Small preflight; never substitute an identity provider here.
modules=[]
for name,mode in (('LmxxfNrRuntime.dll',0),('LmxxfNrRuntime-Original.dll',1)):
    dll=ctypes.WinDLL(str((a.out/name).resolve()),winmode=0x1100);modules.append(dll)
    dll.VeyraLmxxfModelMode.restype=ctypes.c_uint32
    dll.VeyraLmxxfControlsCompatible.restype=ctypes.c_uint32
    dll.VeyraLmxxfModelAssetsReady.argtypes=[ctypes.c_wchar_p]
    dll.VeyraLmxxfModelAssetsReady.restype=ctypes.c_uint32
    if dll.VeyraLmxxfModelMode()!=mode or not dll.VeyraLmxxfControlsCompatible() or not dll.VeyraLmxxfModelAssetsReady(str((a.out/'assets').resolve())):
        raise SystemExit('Actual model selector/asset preflight failed')
    if mode==0 and dll.VeyraLmxxfModelAssetsReady(str(a.out/'missing-assets')):
        raise SystemExit('Missing Small assets wrongly accepted')
assert [dll.VeyraLmxxfModelMode() for dll in modules]==[0,1]
(a.out/'LOCAL-EXPERIMENT.txt').write_text(
    'Veyra AMD NR 0.44 local integration\nMIT source '+PIN+'\n'
    'Performance enabled: Small 310.9.16. Disabled: Original. Separate modules, shared unchanged assets.\n'
    'NVIDIA-derived weights are not MIT source. No game/proxy/add-on or driver files included.\n'
    'gfx1200/gfx1201 only (RX 9000); requires a compatible AMD driver/HIP runtime.\n'
    'No claim of RX 6000/7000 support or AMD inference, quality, latency or performance acceptance.\n'
    'Veyra linear FP16, internal <=1920x1080 pixels, height<=1080, width<=2560; HDR rejected.\n'
    'Veyra owns temporal resets, model selection and strength; external history/AE/hotkeys disabled.\n'
    'Remove runtime/amd-nr to disable this provider, keeping basic playback.\n','utf8')
files=[dict(path=f.relative_to(a.out).as_posix(),size=f.stat().st_size,sha256=sha(f)) for f in sorted(a.out.rglob('*')) if f.is_file()]
report=dict(version='0.44',upstream=PIN,archiveSha256=ARCHIVE_SHA,packageChecksums=len(rows),
    smallWeightFiles=len(small),weightFiles=counts['.f16']+counts['.f32'],hipModules=counts['.hsaco'],
    architectures=['gfx1200','gfx1201'],models={'Small':'LmxxfNrRuntime.dll','Original':'LmxxfNrRuntime-Original.dll'},
    modelSelectorsVerified=True,smallAssetPreflightVerified=True,inferenceVerified=False,files=files)
(a.out/'amd-nr-local-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
print('PASS real model selectors and asset layout;',len(files),'files; no AMD inference test.',a.out)
