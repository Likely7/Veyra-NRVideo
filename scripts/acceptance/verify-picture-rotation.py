"""Verify every ZIP entry against its manifest and cold-start the same bytes.

Avoids another full extracted copy: the staged directory and ZIP are independently
hashed against one manifest. Cold-started profiles and caches live on E: only.
"""
from pathlib import Path
import hashlib,json,os,subprocess,time,zipfile

B=Path('E:/项目/Veyra');T='codex-picture-rotation-20261023'
REPORT=B/'reports'/T
receipts=json.loads((REPORT/'packages.json').read_text('utf8'));results=[]

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,encoding='mbcs',errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another Veyra process is running'

for r in receipts:
    pkg=Path(r['package']);archive=Path(r['zip'])
    assert sha(archive)==r['zipSha256']
    manifest=json.loads((pkg/'package-manifest.json').read_text('utf8'))
    with zipfile.ZipFile(archive) as z:
        names=z.namelist();assert len(names)==len(set(names))
        assert set(names)=={pkg.name+'/'+e['path'] for e in manifest['files']}|{pkg.name+'/package-manifest.json'}
        assert z.read(pkg.name+'/package-manifest.json')==(pkg/'package-manifest.json').read_bytes()
        for e in manifest['files']:
            assert (pkg/e['path']).resolve().is_relative_to(pkg.resolve())
            assert (pkg/e['path']).stat().st_size==e['size'] and sha(pkg/e['path'])==e['sha256']
            with z.open(pkg.name+'/'+e['path']) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==e['sha256'],e['path']
    for mode in ['gpu','software']:
        key=r['vendor']+'-'+mode+'-'+time.strftime('%H%M%S')
        profile=B/'tests'/T/'profiles'/('cold-'+key);tmp=B/'tmp'/T/('cold-'+key);log=B/'logs'/T/('cold-'+key)
        for p in [profile,tmp,log]:p.mkdir(parents=True)
        (profile/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False)),'utf8')
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
        env.update(TEMP=str(tmp),TMP=str(tmp),APPDATA=str(profile/'AppData'),LOCALAPPDATA=str(profile/'LocalAppData'),CUDA_CACHE_PATH=str(tmp/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(log/'engine.log'))
        cmd=[str(pkg/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','4500']
        if mode=='software':cmd.append('--obs-game-capture')
        with (log/'console.log').open('wb') as f:run=subprocess.run(cmd,cwd=pkg,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=45)
        text=(log/'console.log').read_text('utf8',errors='replace')
        failures=[l for l in text.splitlines() if any(s in l for s in ['[ERROR]','TypeError:','ReferenceError:','Binding loop detected','Unable to assign','failed to load component']) and 'Retrying to obtain clipboard' not in l]
        assert run.returncode==0 and not failures,(key,run.returncode,failures)
        if mode=='software':assert 'software UI:' in text
        for e in manifest['files']:assert sha(pkg/e['path'])==e['sha256'],e['path']
        result=dict(vendor=r['vendor'],mode=mode,exit=run.returncode,zipAndStagedFiles=len(manifest['files']),profile=str(profile),log=str(log),passed=True)
        results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
(REPORT/'cold-start.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
