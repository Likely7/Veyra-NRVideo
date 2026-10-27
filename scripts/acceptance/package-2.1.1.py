"""2.1.1 local complete packages. No network, main merge, or user profiles.

Copies audited 2.1.0 donors, replaces only product outputs, then verifies both
archives and cold-starts their extracted payloads. Runtime bytes stay pinned.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shutil, subprocess, time, zipfile
import pefile

ROOT = Path(__file__).resolve().parents[2]
B = Path('E:/项目/Veyra')
TASK = 'codex-build-2.1.1-20261026'
BUILD = B/'build/claude-screen-recording-20261022'
OUT = B/'test-packages'/TASK
REPORT = B/'reports'/TASK
DONOR = B/'releases/release-2.1.0-20261019/runtime-base'
VERSION = '2.1.1'
DOC = 'docs/RELEASE_NOTES_2.1.1.md'
SOURCE = OUT/'Veyra-2.1.1-source.zip'
SOURCES = [
    (B/'archives/dependency-migration-cleanup-20261023/dependency-corresponding-source/Veyra-2.0.6-dependency-source.zip', '4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9'),
    (B/'releases/release-2.0.7-20261013/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip', '49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6'),
    (B/'releases/release-2.0.9-20261017/FFmpeg-9.0.1-deinterlace-module-source.zip', 'f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152'),
    (B/'releases/release-2.1.0-20261019/lmxxf-044-corresponding-source.zip', 'f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b'),
]

def module(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

pkg=module('qml_package_211',ROOT/'scripts/package-qml-release.py')
vendor=module('vendor_package_211',ROOT/'scripts/package-vendor-release.py')

def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def document(p,d):
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n','utf-8')

def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a],text=True,encoding='utf-8').strip()
def appdir(flavor):return OUT/f'Veyra-{VERSION}-{flavor}-win64-portable'

def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)

def check_donor(flavor):
    p=DONOR/flavor;data=json.loads((p/'package-manifest.json').read_text('utf-8'))
    assert data['version']=='2.1.0'
    for row in data['files']:
        pkg.validate_payload(row['path']);f=p/row['path']
        assert f.stat().st_size==row['size'] and sha(f)==row['sha256'],f
    return data

def payload(app):
    rows=[]
    for f in sorted(app.rglob('*')):
        if not f.is_file() or f.name=='package-manifest.json':continue
        rel=f.relative_to(app).as_posix();pkg.validate_payload(rel)
        rows.append(dict(path=rel,size=f.stat().st_size,sha256=sha(f)))
    assert pkg.REQUIRED<={r['path'] for r in rows}
    return rows

def audit(app,flavor):
    names=vendor.check_vendor(app,flavor);pkg.check_models(app)
    m=json.loads((app/'package-manifest.json').read_text('utf-8'))
    assert m['files']==payload(app)
    assert set(names)=={r['path'] for r in m['files']}|{'package-manifest.json'}
    assert m['version']==VERSION and sha(app/'veyra_qml_ui.exe')==m['exeSha256']==sha(BUILD/'veyra_qml_ui.exe')
    pe=pefile.PE(str(app/'veyra_qml_ui.exe'));v=pe.VS_FIXEDFILEINFO[0]
    version=lambda a,b:f'{a>>16}.{a&65535}.{b>>16}.{b&65535}'
    assert version(v.FileVersionMS,v.FileVersionLS)==version(v.ProductVersionMS,v.ProductVersionLS)=='2.1.1.0'
    pe.close()
    packaged={Path(n).name.lower() for n in names if n.lower().endswith('.dll')}
    system=Path(os.environ['WINDIR'])/'System32';missing=[];pe_count=0
    driver={'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}
    for n in sorted(names):
        if Path(n).suffix.lower() not in ('.exe','.dll'):continue
        pe_count+=1;pe=pefile.PE(str(app/n),fast_load=True);pe.parse_data_directories(directories=[1,13])
        deps=[r.dll.decode().lower() for key in ['DIRECTORY_ENTRY_IMPORT','DIRECTORY_ENTRY_DELAY_IMPORT'] for r in getattr(pe,key,[])];pe.close()
        assert not any(d.startswith('npp') for d in deps),(n,deps)
        for d in deps:
            if d not in packaged and not d.startswith(('api-ms-','ext-ms-')) and not (system/d).is_file() and d not in driver:missing.append((n,d))
    assert not missing,missing
    runtime=json.loads((app/'release-runtime-manifest.json').read_text('utf-8'))
    assert runtime['enforcedAtRuntime'] is False
    # Exact equality of every original runtime row prevents silent replacement.
    original=json.loads((DONOR/flavor/'release-runtime-manifest.json').read_text('utf-8'))
    assert runtime['files']==original['files']
    row=dict(passed=True,flavor=flavor,files=len(m['files']),runtimeRows=len(runtime['files']),peFiles=pe_count,peVersion='2.1.1.0',exeSha256=m['exeSha256'])
    document(REPORT/('payload-'+flavor+'.json'),row);return row

def signatures(app,flavor):
    env={k:v for k,v in os.environ.items() if k.upper()!='PSMODULEPATH'}
    env['VEYRA_AUDIT_APP']=str(app)
    script=r'''$ErrorActionPreference='Stop';$a=$env:VEYRA_AUDIT_APP;$m=Get-Content -LiteralPath (Join-Path $a 'release-runtime-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json;
$rows=foreach($r in $m.files){if(-not $r.path.EndsWith('.dll')){continue};$p=Join-Path $a $r.path;$s=Get-AuthenticodeSignature -LiteralPath $p;$v=(Get-Item -LiteralPath $p).VersionInfo;$actual="$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)";if([string]$s.Status -ne $r.authenticode -or $actual -ne $r.fileVersion){throw ('Runtime metadata mismatch: '+$r.path+' '+$s.Status+' '+$actual)};[pscustomobject]@{path=$r.path;version=$actual;signature=[string]$s.Status;signer=$s.SignerCertificate.Subject}};ConvertTo-Json -Depth 4 -InputObject @($rows)'''
    result=subprocess.check_output(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],env=env,text=True,encoding='utf-8',timeout=280)
    document(REPORT/('signatures-'+flavor+'.json'),json.loads(result))

def stage():
    assert not git('status','--porcelain'),'Commit product sources before staging'
    receiptpath=sorted((B/'logs'/TASK).glob('build-*/receipt.json'))[-1]
    receipt=json.loads(receiptpath.read_text('utf-8'))
    assert receipt['label']==VERSION and receipt['executables']['veyra_qml_ui.exe']==sha(BUILD/'veyra_qml_ui.exe')
    assert git('diff',receipt['head'],'--','src','include','apps','qml','i18n','shaders','CMakeLists.txt')=='','Product changed after build'
    for flavor in ['NVIDIA','AMD']:
        donor=check_donor(flavor);app=appdir(flavor);app.mkdir(parents=True,exist_ok=False)
        oldnotes={'README-TEST.zh-CN.md','RELEASE_NOTES.md','RELEASE_2.0.6.md','STREAM_FIX_LOCAL.md','VFG-NEXT-NOTES.zh-CN.md','本地修复说明.md'}
        for row in donor['files']:
            if row['path'] not in oldnotes:copy(DONOR/flavor/row['path'],app/row['path'])
        copy(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
        for folder in ['qml','i18n']:
            for f in (ROOT/folder).rglob('*'):
                if f.is_file():copy(f,app/folder/f.relative_to(ROOT/folder))
        for f in (BUILD/'shaders').glob('*.dxil'):copy(f,app/'shaders'/f.name)
        for name in ['LICENSE','THIRD_PARTY_NOTICES.md','README.md','README_CN.md','README_EN.md']:
            if (ROOT/name).is_file():copy(ROOT/name,app/name)
        for name in [DOC,'docs/BUILD_2.1.1.md']:
            copy(ROOT/name,app/name)
        copy(ROOT/DOC,app/'README-2.1.1.zh-CN.md');copy(ROOT/DOC,app/'RELEASE_NOTES.md')
        (app/'GPU-PACKAGE.txt').write_text(f'Veyra {VERSION} / {flavor}\nComplete Release-configuration portable package. Publication status follows the GitHub Release page.\nExtract all files to a new directory. Runtime/model bytes match audited 2.1.0.\n','utf-8')
        runtime=json.loads((DONOR/flavor/'release-runtime-manifest.json').read_text('utf-8'))
        runtime.update(package=VERSION,candidate=VERSION,localOnly=True)
        for n in ['release-runtime-manifest.json','runtime/experimental/release-runtime-manifest.json']:document(app/n,runtime)
        manifest=dict(schema=4,version=VERSION,displayVersion=VERSION,gpuPackage=flavor,localOnly=True,published=False,releaseReady=False,
                      productSourceCommit=receipt['head'],sourceArchiveCommit=git('rev-parse','HEAD'),mainCommit=git('rev-parse','main'),
                      exeSha256=sha(BUILD/'veyra_qml_ui.exe'),buildReceipt=str(receiptpath),validationRecord=DOC,
                      correspondingSource=dict(application='../'+SOURCE.name,applicationSha256=None,
                         dependencies=[dict(archive='../'+p.name,sha256=h) for p,h in SOURCES]),
                      knownIssues=['4K YUY2 ~53fps field issue diagnosed only','T911 capture stops with enhancement: initial cause unresolved','CV710 and RTX3090 require original-machine retest','AMD HDR+NR requires RX9000 verification','Long-run VRAM growth unresolved'],
                      files=payload(app))
        document(app/'package-manifest.json',manifest);audit(app,flavor);signatures(app,flavor);check_donor(flavor)
        print('STAGE PASS',flavor,flush=True)

def source_archive():
    assert not git('status','--porcelain') and not SOURCE.exists()
    tmp=B/'tmp'/TASK/'application-source.zip';tmp.parent.mkdir(parents=True,exist_ok=True);assert not tmp.exists()
    subprocess.run(['git','-C',str(ROOT),'archive','--format=zip','--output='+str(tmp),'HEAD'],check=True)
    rows=[]
    with zipfile.ZipFile(tmp) as src,zipfile.ZipFile(SOURCE,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as dst:
        for e in src.infolist():
            if e.is_dir():continue
            n=e.filename;pkg.relative(n)
            if '__pycache__' in n or n.endswith('.pyc'):continue
            assert not re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|zip|7z|ptx|addon64)$|(^|/)(runtime_local|third_party_local|\.git)/',n,re.I),n
            data=src.read(e);dst.writestr('Veyra-2.1.1-source/'+n,data)
            rows.append(dict(path=n,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
        dst.writestr('Veyra-2.1.1-source/source-snapshot-manifest.json',json.dumps(dict(commit=git('rev-parse','HEAD'),files=rows),ensure_ascii=False,indent=2))
    with zipfile.ZipFile(SOURCE) as z:
        assert z.testzip() is None
        for r in rows:assert hashlib.sha256(z.read('Veyra-2.1.1-source/'+r['path'])).hexdigest()==r['sha256']
    tmp.unlink();return dict(path=str(SOURCE),sha256=sha(SOURCE),files=len(rows),commit=git('rev-parse','HEAD'))

def seal():
    source=source_archive();deps=[];rows=[]
    for p,h in SOURCES:
        assert sha(p)==h,p
        with zipfile.ZipFile(p) as z:assert z.testzip() is None
        dst=OUT/p.name;assert not dst.exists();copy(p,dst);assert sha(dst)==h
        deps.append(dict(path=str(dst),sha256=h))
    for flavor in ['NVIDIA','AMD']:
        check_donor(flavor);app=appdir(flavor);m=json.loads((app/'package-manifest.json').read_text('utf-8'))
        m['sourceArchiveCommit']=source['commit'];m['correspondingSource']['applicationSha256']=source['sha256'];document(app/'package-manifest.json',m)
        checked=audit(app,flavor);zpath=Path(str(app)+'.zip')
        with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for r in m['files']:z.write(app/r['path'],app.name+'/'+r['path'])
            z.write(app/'package-manifest.json',app.name+'/package-manifest.json')
        result=pkg.verify_zip(zpath)
        rows.append(dict(vendor=flavor,app=str(app),archive=str(zpath),bytes=zpath.stat().st_size,sha256=result['sha256'],**checked))
        print('ZIP PASS',flavor,zpath,flush=True)
    for p in [SOURCE,*[Path(r['path']) for r in deps],*[Path(r['archive']) for r in rows]]:
        Path(str(p)+'.sha256').write_text(sha(p)+'  '+p.name+'\n','ascii')
    document(OUT/'DELIVERY.json',dict(version=VERSION,localOnly=True,published=False,source=source,dependencies=deps,packages=rows))

def cold():
    delivery=json.loads((OUT/'DELIVERY.json').read_text('utf-8'));results=[]
    media=B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4';assert media.is_file()
    running=json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',"ConvertTo-Json -Compress -InputObject @((Get-Process | Where-Object { $_.ProcessName -like 'veyra*' } | Select-Object Id,ProcessName,Path))"],text=True,encoding='utf-8'))
    for row in delivery['packages']:
        flavor=row['vendor'];zpath=Path(row['archive']);assert sha(zpath)==row['sha256'];pkg.verify_zip(zpath)
        dest=B/'verify'/TASK/flavor;dest.mkdir(parents=True,exist_ok=False)
        with zipfile.ZipFile(zpath) as z:
            for e in z.infolist():pkg.relative(e.filename)
            z.extractall(dest)
        app=dest/Path(row['app']).name;audit(app,flavor)
        for mode in ['d3d12','software']:
            tag=flavor+'-'+mode;profile=B/'tests'/TASK/tag;tmp=B/'tmp'/TASK/tag;logs=B/'logs'/TASK/tag
            for p in [profile,tmp,logs]:p.mkdir(parents=True,exist_ok=False)
            document(profile/'qml-preferences.v1.json',dict(language='zh-CN',autoResume=False,rememberPosition=False,prewarmEnhancement=False,gpuPriority='normal',sentinel='retained'))
            env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};win=Path(env['WINDIR'])
            env.update(PATH=str(win/'System32')+';'+str(win),TEMP=str(tmp),TMP=str(tmp),APPDATA=str(tmp/'AppData'),LOCALAPPDATA=str(tmp/'LocalAppData'),CUDA_CACHE_PATH=str(tmp/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(logs/'engine.log'))
            command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x900','--exit-after','10000',str(media)]
            if mode=='software':env['QT_QUICK_BACKEND']='software';command.insert(1,'--obs-game-capture')
            else:env['VEYRA_UI_RHI']='d3d12'
            start=time.monotonic()
            with (logs/'console.log').open('wb') as f:run=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=45)
            text='\n'.join(p.read_text('utf-8',errors='replace') for p in [logs/'engine.log',logs/'console.log'] if p.exists())
            errors=[l for l in text.splitlines() if re.search(r'ReferenceError:|TypeError:|Cannot assign|Binding loop|D3D12 ERROR|D3D12 CORRUPTION|\[ERROR\s*\]|\[FATAL\s*\]|no root object',l)]
            presented=[int(v) for v in re.findall(r'\brealPresented=(\d+)',text)]
            result=dict(vendor=flavor,mode=mode,exit=run.returncode,seconds=round(time.monotonic()-start,2),errors=errors,realPresented=max(presented,default=0),logs=str(logs))
            results.append(result);document(REPORT/'cold-start.json',dict(passed=False,results=results,existingUserProcesses=running,performanceMeasurement=False))
            assert run.returncode==0 and not errors and result['realPresented']>=100,result
            assert ('interface renderer=d3d12' in text) if mode=='d3d12' else ('interface=software' in text)
            assert json.loads((profile/'qml-preferences.v1.json').read_text('utf-8'))['sentinel']=='retained'
            audit(app,flavor);print('COLD PASS',tag,result['realPresented'],flush=True)
    document(REPORT/'cold-start.json',dict(passed=True,results=results,existingUserProcesses=running,performanceMeasurement=False))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['stage','seal','cold'])
    parser.add_argument('--task',default=TASK,help='Separate output/receipt directory for a fresh packaging run')
    args=parser.parse_args()
    assert re.fullmatch(r'codex-[a-z0-9.-]+',args.task),args.task
    TASK=args.task;OUT=B/'test-packages'/TASK;REPORT=B/'reports'/TASK;SOURCE=OUT/'Veyra-2.1.1-source.zip'
    OUT.mkdir(parents=True,exist_ok=True);REPORT.mkdir(parents=True,exist_ok=True)
    {'stage':stage,'seal':seal,'cold':cold}[args.mode]()
