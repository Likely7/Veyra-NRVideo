"""Local 2.1.2 test packages, using audited 2.1.1 donors and pinned RX7000 assets.

No publication or real user settings. Run stage, seal, cold sequentially.
Each child test is bounded; source and proprietary runtime archives stay separate.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shutil,stat,subprocess,time,zipfile
import pefile

R=Path(__file__).resolve().parents[2];B=Path('E:/项目/Veyra');T='codex-build-2.1.2-20261102';V='2.1.2'
D=B/'build/claude-screen-recording-20261022';O=B/'test-packages'/T;REPORT=B/'reports'/T
DONOR=B/'test-packages/codex-release-2.1.1-final-20261027';DEP=B/'deps/amd-rdna3-nr-20261029'
RDNA_COMPONENTS=B/'reports/amd-rdna3-nr-20261029/runtime-components.json'
WORKER='runtime/amd-nr-rdna3/veyra_amd_nr_worker.exe'
STUB='runtime/amd-nr-rdna3/zluda/nvcuda.dll'
STUB_SHA='b5f118a8f08b3789319cc61e151e1c84ff0f85dc2620a09bd51e7d877fc1ebbe'
spec=importlib.util.spec_from_file_location('qml_pkg',R/'scripts/package-qml-release.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)

def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(p.read_text('utf-8-sig'))
def write(p,data):
    p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def git(*a):return subprocess.check_output(['git','-C',str(R),*a]).decode('utf-8').strip()
def appdir(flavor):return O/f'Veyra-{V}-{flavor}-win64-portable'
def donor(flavor):return DONOR/f'Veyra-2.1.1-{flavor}-win64-portable'
def validate(name):
    if name==WORKER:pkg.relative(name) # The reviewed, locally built native host only.
    else:pkg.validate_payload(name)
def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
def record(p,name):return dict(path=name,size=p.stat().st_size,sha256=sha(p))
def payload(app):
    rows=[]
    for p in sorted(app.rglob('*')):
        if not p.is_file() or p.name=='package-manifest.json':continue
        assert not p.lstat().st_file_attributes&stat.FILE_ATTRIBUTE_REPARSE_POINT,p
        n=p.relative_to(app).as_posix();validate(n);rows.append(record(p,n))
    assert pkg.REQUIRED<={r['path'] for r in rows}
    return rows
def check_donor(flavor):
    a=donor(flavor);m=read(a/'package-manifest.json');assert m['version']=='2.1.1'
    for row in m['files']:
        validate(row['path']);p=a/row['path'];assert p.stat().st_size==row['size'] and sha(p)==row['sha256'].lower(),p
    return m
def version(p):
    pe=pefile.PE(str(p),fast_load=False);i=pe.VS_FIXEDFILEINFO[0]
    v=(i.FileVersionMS>>16,i.FileVersionMS&65535,i.FileVersionLS>>16,i.FileVersionLS&65535)
    pv=(i.ProductVersionMS>>16,i.ProductVersionMS&65535,i.ProductVersionLS>>16,i.ProductVersionLS&65535);pe.close()
    assert v==pv==(2,1,2,0),(p,v,pv)
    return '.'.join(map(str,v))
def audit(app,flavor):
    m=read(app/'package-manifest.json');rows=payload(app);assert m['files']==rows
    assert m['version']==V and m['localOnly'] and not m['published']
    assert sha(app/'veyra_qml_ui.exe')==sha(D/'veyra_qml_ui.exe')==m['exeSha256']
    pe_version=version(app/'veyra_qml_ui.exe');names={r['path'] for r in rows};pkg.check_models(app)
    if flavor=='NVIDIA':
        assert not any(n.startswith(('runtime/amd-nr/','runtime/amd-nr-rdna3/')) for n in names)
        assert 'runtime/nvidia-vfg/nvVFXVideoFrameGeneration.dll' in names
    else:
        assert {'runtime/amd-nr/LmxxfNrRuntime.dll','runtime/amd-nr/LmxxfNrRuntime-Original.dll',WORKER}<=names
        assert not any(n.startswith(('runtime/nvidia-vfg/','runtime/experimental/nvngx','runtime/experimental/nr-','runtime/experimental/fallback-dlssg','runtime/experimental/dlssg-')) for n in names)
        assert sha(app/WORKER)==sha(D/'veyra_amd_nr_worker.exe')
        assert sha(app/STUB)==STUB_SHA
    runtime=read(app/'release-runtime-manifest.json');assert runtime==read(app/'runtime/experimental/release-runtime-manifest.json')
    assert runtime['enforcedAtRuntime'] is False and runtime['localOnly'] and not runtime['releaseAuthorized']
    for row in runtime['files']:
        p=app/row['path'];assert p.stat().st_size==row['size'] and sha(p)==row['sha256'].lower(),p
    original=read(donor(flavor)/'release-runtime-manifest.json')['files']
    assert runtime['files'][:len(original)]==original,'Old default runtime identity changed'
    for n in names:
        if Path(n).name.lower() in {'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}:
            assert flavor=='AMD' and n==STUB and sha(app/n)==STUB_SHA,'Only pinned private ZLUDA shim is allowed'
    runtime_names={n for n in names if n.startswith(('runtime/','runtime_local/')) and Path(n).suffix.lower() not in {'.json','.txt','.md'}}
    assert runtime_names<={r['path'] for r in runtime['files']},runtime_names-{r['path'] for r in runtime['files']}
    dlls={Path(n).name.lower() for n in names if n.lower().endswith('.dll')};missing=[];pe_count=0
    drivers={'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'};system=Path(os.environ['WINDIR'])/'System32'
    for n in sorted(names):
        if Path(n).suffix.lower() not in {'.exe','.dll'}:continue
        pe_count+=1;pe=pefile.PE(str(app/n),fast_load=True);pe.parse_data_directories(directories=[1,13])
        deps=[e.dll.decode().lower() for key in ('DIRECTORY_ENTRY_IMPORT','DIRECTORY_ENTRY_DELAY_IMPORT') for e in getattr(pe,key,[])];pe.close()
        assert not any(x.startswith('npp') for x in deps),(n,deps)
        for dep in deps:
            # Private worker shims are never a dependency resolution fallback for the main EXE.
            if dep not in dlls and dep not in drivers and not dep.startswith(('api-ms-','ext-ms-')) and not (system/dep).is_file():missing.append((n,dep))
    assert not missing,missing
    return dict(flavor=flavor,files=len(rows),runtimeRows=len(runtime['files']),peFiles=pe_count,peVersion=pe_version,exeSha256=m['exeSha256'])
def stage():
    assert not git('status','--porcelain'),'Commit reviewed product before staging'
    receiptpath=sorted((B/'logs'/T).glob('build-*/receipt.json'))[-1];receipt=read(receiptpath)
    assert receipt['label']==V and receipt['executables']['veyra_qml_ui.exe']==sha(D/'veyra_qml_ui.exe')
    assert not git('diff',receipt['head'],'--','src','include','apps','qml','i18n','shaders','CMakeLists.txt')
    for flavor in ('NVIDIA','AMD'):
        old=check_donor(flavor);a=appdir(flavor);a.mkdir(parents=True,exist_ok=False)
        for row in old['files']:
            n=row['path']
            if n in {'RELEASE_NOTES.md','GPU-PACKAGE.txt','README-2.1.1.zh-CN.md'} or n.startswith('docs/'):continue
            copy(donor(flavor)/n,a/n)
        copy(D/'veyra_qml_ui.exe',a/'veyra_qml_ui.exe')
        for folder in ('qml','i18n'):
            for p in (R/folder).rglob('*'):
                if p.is_file():copy(p,a/folder/p.relative_to(R/folder))
        for p in (D/'shaders').glob('*.dxil'):copy(p,a/'shaders'/p.name)
        for name in ('LICENSE','THIRD_PARTY_NOTICES.md','README.md','README_CN.md','README_EN.md'):copy(R/name,a/name)
        for name in ('BUILD_2.1.2_TEST_2026-11-02.md','AMD_RDNA3_NR_PLAN_2026-10-29.md'):copy(R/'docs'/name,a/'docs'/name)
        copy(R/'docs/TEST_NOTES_2.1.2.md',a/'README-2.1.2.zh-CN.md');copy(R/'docs/TEST_NOTES_2.1.2.md',a/'RELEASE_NOTES.md')
        runtime=read(donor(flavor)/'release-runtime-manifest.json');runtime.update(package=V,candidate=V,localOnly=True,releaseAuthorized=False)
        if flavor=='AMD':
            subprocess.run([os.sys.executable,'-X','utf8','-B',str(R/'scripts/amd-nr/stage-rdna3-runtime.py'),'--dependency-root',str(DEP),'--components',str(RDNA_COMPONENTS),'--build-dir',str(D),'--app',str(a),'--report',str(REPORT/'rdna3-runtime.json')],cwd=R,check=True,timeout=120)
            rd=read(REPORT/'rdna3-runtime.json')
            for row in rd['files']:
                runtime['files'].append(dict(row,path='runtime/amd-nr-rdna3/'+row['path'],name=Path(row['path']).name,source='RedDuke 1.3.0 / ZLUDA fixed commits; native host built from Veyra source',experimental=True,removable=True,localOnly=True,authenticode=rd['networkIdentity']['signature'] if row['path']=='nvngx_dlssnr.dll' else 'NotApplicable',fileVersion=rd['networkIdentity']['version'] if row['path']=='nvngx_dlssnr.dll' else None))
        for name in ('release-runtime-manifest.json','runtime/experimental/release-runtime-manifest.json'):write(a/name,runtime)
        (a/'GPU-PACKAGE.txt').write_text(f'Veyra {V} / {flavor} / LOCAL TEST ONLY\nExtract to a NEW directory. Run veyra_qml_ui.exe.\nNo saved user configuration is included. Default effects remain off.\nAMD: RX7000 RedDuke and RX9000 lmxxf are separate experimental paths; HIP7 driver components must already be installed.\n',encoding='utf-8')
        m=dict(schema=5,version=V,displayVersion=V,gpuPackage=flavor,localOnly=True,published=False,releaseReady=False,productSourceCommit=receipt['head'],mainCommit=git('rev-parse','main'),exeSha256=sha(D/'veyra_qml_ui.exe'),buildReceipt=str(receiptpath),correspondingSource=dict(application='../Veyra-2.1.2-source.zip',amdNetwork='../Veyra-2.1.2-rdna3-network-source.zip' if flavor=='AMD' else None,unchangedDependencies='docs/BUILD_2.1.2_TEST_2026-11-02.md'),files=payload(a))
        write(a/'package-manifest.json',m);write(REPORT/f'payload-{flavor}.json',audit(a,flavor));print('STAGE PASS',flavor,flush=True)
def verify_zip(path):
    with zipfile.ZipFile(path) as z:
        names=[e.filename for e in z.infolist() if not e.is_dir()];assert len(names)==len(set(n.lower() for n in names))
        for e in z.infolist():pkg.relative(e.filename.rstrip('/'));assert not stat.S_ISLNK(e.external_attr>>16)
        key=next(n for n in names if n.count('/')==1 and n.endswith('/package-manifest.json'));prefix=key.split('/')[0]+'/';m=json.loads(z.read(key))
        assert set(names)=={prefix+r['path'] for r in m['files']}|{key}
        for row in m['files']:
            validate(row['path']);info=z.getinfo(prefix+row['path'])
            with z.open(info) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256']
            assert info.file_size==row['size']
    return dict(archive=str(path),bytes=path.stat().st_size,sha256=sha(path),verifiedFiles=len(m['files']))
def seal():
    assert not git('status','--porcelain')
    source=O/'Veyra-2.1.2-source.zip';assert not source.exists()
    subprocess.run(['git','-C',str(R),'archive','--format=zip','--prefix=Veyra-2.1.2-source/','--output='+str(source),'HEAD'],check=True,timeout=120)
    with zipfile.ZipFile(source) as z:
        assert z.testzip() is None
        for n in z.namelist():assert not re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|ptx|addon64)$|(^|/)(runtime_local|third_party_local|\.git)/',n,re.I),n
    network=O/'Veyra-2.1.2-rdna3-network-source.zip'
    with zipfile.ZipFile(network,'x',zipfile.ZIP_DEFLATED) as z:
        for p in (DEP/'prepared').rglob('*'):
            if p.is_file():
                n=p.relative_to(DEP/'prepared').as_posix();pkg.relative(n);assert p.suffix.lower() in {'.cpp','.h','.md',''};z.write(p,'rdna3-network/'+n)
        z.write(DEP/'source-manifest.json','rdna3-network/source-manifest.json')
    results=[]
    for flavor in ('NVIDIA','AMD'):
        a=appdir(flavor);m=read(a/'package-manifest.json');m['correspondingSource'].update(applicationSha256=sha(source),amdNetworkSha256=sha(network) if flavor=='AMD' else None);write(a/'package-manifest.json',m);audit(a,flavor)
        path=Path(str(a)+'.zip')
        with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for row in m['files']:z.write(a/row['path'],a.name+'/'+row['path'])
            z.write(a/'package-manifest.json',a.name+'/package-manifest.json')
        v=verify_zip(path);v.update(flavor=flavor,app=str(a));results.append(v);print('ZIP PASS',flavor,v['bytes'],flush=True)
    for p in (source,network,*[Path(x['archive']) for x in results]):Path(str(p)+'.sha256').write_text(sha(p)+'  '+p.name+'\n',encoding='ascii')
    write(O/'DELIVERY.json',dict(version=V,localOnly=True,published=False,mainCommit=git('rev-parse','main'),source=dict(path=str(source),sha256=sha(source)),networkSource=dict(path=str(network),sha256=sha(network)),packages=results))
def cold():
    results=[]
    for flavor in ('NVIDIA','AMD'):
        row=next(x for x in read(O/'DELIVERY.json')['packages'] if x['flavor']==flavor);path=Path(row['archive']);assert sha(path)==row['sha256'];verify_zip(path)
        dest=B/'verify'/T/flavor;dest.mkdir(parents=True,exist_ok=False)
        with zipfile.ZipFile(path) as z:z.extractall(dest)
        a=dest/appdir(flavor).name;audit(a,flavor)
        tmp=B/'tmp'/T/('cold-'+flavor);profile=B/'tests'/T/('cold-'+flavor);logs=B/'logs'/T/('cold-'+flavor)
        for p in (tmp,profile,logs):p.mkdir(parents=True,exist_ok=False)
        write(profile/'qml-preferences.v1.json',dict(language='zh',autoResume=False,prewarmEnhancement=False,rememberPosition=False,gpuPriority='normal',sentinel=T))
        e={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};win=Path(e['WINDIR'])
        e.update(PATH=str(win/'System32')+';'+str(win),TEMP=str(tmp),TMP=str(tmp),APPDATA=str(tmp/'AppData'),LOCALAPPDATA=str(tmp/'LocalAppData'),CUDA_CACHE_PATH=str(tmp/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(logs/'engine.log'))
        command=[str(a/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','min','--reduced-motion','--exit-after','8000',str(B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4')]
        start=time.monotonic()
        with (logs/'console.log').open('wb') as f:p=subprocess.run(command,cwd=a,env=e,stdout=f,stderr=subprocess.STDOUT,timeout=60)
        text='\n'.join(q.read_text('utf-8',errors='replace') for q in (logs/'engine.log',logs/'console.log') if q.exists())
        errors=[s for s in text.splitlines() if re.search(r'ReferenceError:|TypeError:|Cannot assign|Binding loop|D3D12 ERROR|D3D12 CORRUPTION|\[ERROR\s*\]|\[FATAL\s*\]|no root object',s)]
        presented=max([int(x) for x in re.findall(r'\brealPresented=(\d+)',text)],default=0)
        v=dict(flavor=flavor,exit=p.returncode,seconds=time.monotonic()-start,errors=errors,realPresented=presented,logs=str(logs));results.append(v);write(REPORT/'cold.json',dict(results=results))
        assert p.returncode==0 and not errors and presented>=60,v
        assert read(profile/'qml-preferences.v1.json')['sentinel']==T
        audit(a,flavor);print('COLD PASS',flavor,presented,flush=True)
    write(REPORT/'cold.json',dict(passed=True,results=results,scope='Local RTX5070 startup/playback with default effects off, not AMD inference or physical capture'))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['stage','seal','cold']);args=p.parse_args()
    for folder in (O,REPORT):folder.mkdir(parents=True,exist_ok=True)
    dict(stage=stage,seal=seal,cold=cold)[args.mode]()
