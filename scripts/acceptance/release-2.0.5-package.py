"""Package the reviewed 2.0.5 build with unchanged audited vendor components."""
from pathlib import Path,PurePosixPath
import ctypes,hashlib,json,shutil,subprocess,sys,zipfile
from ctypes import wintypes
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007';VERSION='2.0.5'
PACK=BASE/'test-packages'/TASK;BUILD=BASE/'build'/TASK/'standard';LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK
DEPS=BASE/'releases/publish-2.0.4-20261006/Veyra-2.0.4-dependency-source.zip'
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf8'))
def save(path,value):Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,stderr=subprocess.PIPE).decode('utf8').strip()
def safe(name):
    p=PurePosixPath(name)
    assert name and '\\' not in name and ':' not in name and not p.is_absolute() and '..' not in p.parts and str(p)==name,name
def pe_versions(path):
    api=ctypes.WinDLL('version');unused=wintypes.DWORD()
    api.GetFileVersionInfoSizeW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(wintypes.DWORD)];api.GetFileVersionInfoSizeW.restype=wintypes.DWORD
    length=api.GetFileVersionInfoSizeW(str(path),ctypes.byref(unused));assert length
    block=ctypes.create_string_buffer(length)
    api.GetFileVersionInfoW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p]
    assert api.GetFileVersionInfoW(str(path),0,length,block)
    ptr=ctypes.c_void_p();size=wintypes.UINT()
    api.VerQueryValueW.argtypes=[ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(wintypes.UINT)]
    assert api.VerQueryValueW(block,'\\',ctypes.byref(ptr),ctypes.byref(size)) and size.value>=52
    data=ctypes.cast(ptr,ctypes.POINTER(wintypes.DWORD))
    def value(a,b):return '.'.join(map(str,(a>>16,a&65535,b>>16,b&65535)))
    return dict(file=value(data[2],data[3]),product=value(data[4],data[5]))
mode=sys.argv[1];assert mode in ('stage','refresh','finalize')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.5-control.py'),'--published'],check=True)
head=git('rev-parse','HEAD');dirty=bool(git('status','--porcelain'))
exe=BUILD/'veyra_qml_ui.exe';versions=pe_versions(exe)
assert versions==dict(file='2.0.5.0',product='2.0.5.0'),versions
assert sha(DEPS)=='4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9'
sourcezip=PACK/f'Veyra-{VERSION}-source.zip'
if mode=='finalize':
    assert not dirty,'Commit the tested integration first'
    merge=read(LOG/'main-merge.json');assert merge['testedSourceCommit']==head and merge['treesIdentical']
    frozen=read(LOG/'tested-inputs.json');assert frozen['sourceCommit']==head and frozen['executableSha256']==sha(exe)
    for name,h in frozen['trackedInputs'].items():assert sha(ROOT/name)==h,name
    for name,h in frozen['testReceipts'].items():assert sha(LOG/name)==h,name
    with zipfile.ZipFile(sourcezip,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as zipped:
        for name in git('ls-files').splitlines():
            safe(name)
            assert not name.startswith(('runtime/','runtime_local/','third_party_local/','models/'))
            assert Path(name).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.addon64','.onnx','.bin','.hsaco','.f16','.f32','.ptx','.pyc'),name
            zipped.write(ROOT/name,arcname='Veyra-2.0.5-source/'+name)
    with zipfile.ZipFile(sourcezip) as zipped:
        assert zipped.testzip() is None
        for name,h in frozen['trackedInputs'].items():assert hashlib.sha256(zipped.read('Veyra-2.0.5-source/'+name)).hexdigest()==h,name
    subprocess.run(['git','bundle','create',str(ARCH/'integration-final.bundle'),'578d63c3a0143429b306e26eeb89137473b4d99f..HEAD'],cwd=ROOT,check=True)
    subprocess.run(['git','bundle','verify',str(ARCH/'integration-final.bundle')],cwd=ROOT,check=True)
delivery=[]
for vendor in ('AMD','NVIDIA'):
    source=BASE/'test-packages/stability-export-priority-20261006'/f'Veyra-2.0.4-fix2-{vendor}-win64-portable'
    original=read(source/'package-manifest.json');app=PACK/f'Veyra-{VERSION}-{vendor}-win64-portable'
    assert original['executableSha256']=='65fc23e33598d7efd77ea50ba8062a89beaacb29eb14369ffc78d03563312626'
    immutable={}
    if mode=='stage':app.mkdir(exist_ok=False)
    else:assert app.is_dir()
    for row in original['files']:
        name=row['path'];safe(name);p=source/name
        assert p.resolve().is_relative_to(source.resolve()) and not p.is_symlink()
        assert p.stat().st_size==row['size'] and sha(p)==row['sha256'],name
        if mode=='stage' and not name.startswith('REPAIR_TEST_'):
            dest=app/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        if name.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower()=='.dll' or name in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):
            immutable[name]=row['sha256']
    if mode!='finalize':
        shutil.copy2(exe,app/'veyra_qml_ui.exe')
        for folder,incoming in (('qml',ROOT/'qml'),('shaders',BUILD/'shaders')):
            for p in incoming.rglob('*'):
                if p.is_file():
                    dest=app/folder/p.relative_to(incoming);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        for name in ('RELEASE_2.0.5_PLAN_2026-10-07.md','RELEASE_2.0.5_ACCEPTANCE_2026-10-07.md','TEST_NOTES_2.0.5.md','BUILD_2.0.5.md','NV_VRAM_DEEP_INVESTIGATION_2026-10-07.md'):
            shutil.copy2(ROOT/'docs'/name,app/'docs'/name)
        shutil.copy2(ROOT/'docs/TEST_NOTES_2.0.5.md',app/'TEST_NOTES_2.0.5.md')
    for name,h in immutable.items():assert sha(app/name)==h,name
    assert sha(app/'veyra_qml_ui.exe')==sha(exe)
    actual={p.relative_to(app).as_posix():p for p in app.rglob('*') if p.is_file()}
    for name,p in actual.items():
        safe(name)
        assert not p.is_symlink() and p.suffix.lower() not in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl'),name
        assert not any(part.lower() in ('logs','tmp','outputs','profile','.git') or part.lower().startswith(('user-data','qml-preferences','last-applied','presets.v1')) for part in PurePosixPath(name).parts),name
        assert p.suffix.lower()!='.exe' or name=='veyra_qml_ui.exe',name
    manifest=dict(original)
    manifest.update(version=VERSION,displayVersion=VERSION,candidate=VERSION,sourceCommit=head,sourceArchiveCommit=head,productCodeCommit=head,
        worktreeDirty=dirty,localOnly=True,releaseReady=False,executableSha256=sha(exe),peVersions=versions,
        baseCommit='71483d56f17d85784f6ff1cbb40f83b90747b0d4',baseMainCommit='578d63c3a0143429b306e26eeb89137473b4d99f',
        validationRecord='docs/RELEASE_2.0.5_ACCEPTANCE_2026-10-07.md',
        validation='Current 2.0.5 targeted checks only; see the current acceptance record and DELIVERY',
        softwareValidation='2.0.5 targeted checks recorded separately from inherited fix2 measurements',
        hardwareValidation='Local RTX5070; actual AMD inference/encoder and user display tearing remain unverified',
        knownIssue='NVIDIA VRAM growth remains unresolved and deferred to the next version',
        files=[dict(path=name,size=p.stat().st_size,sha256=sha(p)) for name,p in sorted(actual.items()) if name!='package-manifest.json'])
    manifest.pop('releaseTag',None);manifest.pop('releaseUrl',None)
    corresponding=dict(original['correspondingSource']);corresponding.update(application='../'+sourcezip.name,
        applicationSha256=sha(sourcezip) if mode=='finalize' else None,instructions='docs/BUILD_2.0.5.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.5')
    manifest['correspondingSource']=corresponding
    manifest.pop('sourceZipSha256',None)
    if mode=='finalize':
        manifest['sourceZipSha256']=sha(sourcezip)
        manifest['mainCommit']=merge['mainAfter']
    save(app/'package-manifest.json',manifest)
    receipt=dict(vendor=vendor,app=str(app),sourceCommit=head,exeSha256=sha(exe),peVersions=versions,
        fileCount=len(actual),unchangedComponentCount=len(immutable),dependencySource=str(DEPS),dependencySourceSha256=sha(DEPS))
    if mode=='finalize':
        receipt['mainCommit']=merge['mainAfter']
        archive=PACK/(app.name+'.zip')
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as zipped:
            for name,p in sorted(actual.items()):zipped.write(p,arcname=app.name+'/'+name)
        with zipfile.ZipFile(archive) as zipped:
            assert zipped.testzip() is None
            assert set(zipped.namelist())=={app.name+'/'+name for name in actual}
            for name,p in actual.items():assert hashlib.sha256(zipped.read(app.name+'/'+name)).hexdigest()==sha(p),name
        receipt.update(archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),zipCrcAndAllPayloadHashesPassed=True,
            sourceZip=str(sourcezip),sourceZipSha256=sha(sourcezip))
    delivery.append(receipt);print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(LOG/('packages-'+mode+'.json'),delivery)
if mode=='finalize':
    save(PACK/'DELIVERY.json',delivery)
    sums=''.join(row['archiveSha256']+'  '+Path(row['archive']).name+'\n' for row in delivery)+sha(sourcezip)+'  '+sourcezip.name+'\n'
    (PACK/'SHA256SUMS.txt').write_text(sums,encoding='ascii')
