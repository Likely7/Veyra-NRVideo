"""Lossless ZIP + solid LZMA2 copies, hash every payload after 7z extraction."""
import hashlib, importlib.util, json, subprocess, sys, time, zipfile
from pathlib import Path
import py7zr
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004';LABEL='2.0.2-slim-20261004'
OUT=BASE/'test-packages'/TASK;STAGE=OUT/('Veyra-'+LABEL+'-win64-portable');LOGS=BASE/'logs'/TASK
spec=importlib.util.spec_from_file_location('qml_package',ROOT/'scripts/package-qml-release.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit before archiving'
def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if sys.argv[1]=='archive':
    manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'))
    files=sorted(p for p in STAGE.rglob('*') if p.is_file())
    expected={r['path'] for r in manifest['files']}|{'package-manifest.json'}
    assert {p.relative_to(STAGE).as_posix() for p in files}==expected
    for p in files:assert not p.is_symlink();pkg.validate_payload(p.relative_to(STAGE).as_posix())
    results=[]
    t=time.monotonic();archive=Path(str(STAGE)+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in files:z.write(p,STAGE.name+'/'+p.relative_to(STAGE).as_posix())
    result=pkg.verify_zip(archive);result.update(method='ZIP-deflate9',bytes=archive.stat().st_size,seconds=time.monotonic()-t);results.append(result)
    print('ZIP ALL PAYLOADS PASS',result['bytes'],flush=True)
    t=time.monotonic();archive=Path(str(STAGE)+'.7z');assert not archive.exists()
    with py7zr.SevenZipFile(archive,'w',filters=[{'id':py7zr.FILTER_LZMA2,'preset':5,'dict_size':32*1024*1024}]) as z:
        for i,p in enumerate(files):
            z.write(p,STAGE.name+'/'+p.relative_to(STAGE).as_posix())
            if i and i%250==0:print('7Z PACKING FILES',i,'/',len(files),flush=True)
    print('7Z WRITTEN; EXTRACT/HASH NEXT',archive.stat().st_size,flush=True)
    verify=BASE/'verify'/TASK;verify.mkdir(parents=True,exist_ok=False)
    with py7zr.SevenZipFile(archive,'r') as z:
        names=z.getnames()
        assert len(names)==len(expected) and set(names)=={STAGE.name+'/'+r for r in expected}
        for name in names:pkg.relative(name)
        z.extractall(path=verify)
    app=verify/STAGE.name
    for row in manifest['files']:
        p=app/row['path'];assert p.is_file() and not p.is_symlink() and p.stat().st_size==row['size'] and pkg.digest(p)==row['sha256'],row['path']
    assert pkg.digest(app/'package-manifest.json')==pkg.digest(STAGE/'package-manifest.json')
    result={'archive':str(archive),'sha256':pkg.digest(archive),'bytes':archive.stat().st_size,'method':'7z-solid-LZMA2-32MiB','seconds':time.monotonic()-t,'verifiedFiles':len(manifest['files']),'status':'pass','extractedApp':str(app)};results.append(result)
    for result in results:
        p=Path(result['archive']);Path(str(p)+'.sha256').write_text(result['sha256']+'  '+p.name+'\n',encoding='ascii')
    write(LOGS/'archive-audit.json',{'archives':results,'rawBytes':sum(p.stat().st_size for p in files),'codeCommit':manifest['baseCommit'],'appSha256':pkg.digest(STAGE/'veyra_qml_ui.exe')})
    print('BOTH ARCHIVES ALL PAYLOADS PASS',json.dumps(results,ensure_ascii=False),flush=True)
elif sys.argv[1]=='source':
    manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'));commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
    changed=subprocess.check_output(['git','diff','--name-only',manifest['baseCommit'],commit],cwd=ROOT).decode().splitlines()
    assert all(p.startswith(('docs/','scripts/acceptance/runtime-size-')) for p in changed),'Product source changed after package build'
    names=subprocess.check_output(['git','ls-files'],cwd=ROOT).decode().splitlines()
    assert not [p for p in names if Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.whl','.f16','.f32','.i32','.onnx','.hsaco','.cso','.ptx','.addon64'}],'Runtime/model in source'
    source=OUT/('Veyra-'+LABEL+'-source.zip');assert not source.exists()
    subprocess.run(['git','archive','--format=zip','--output',str(source),commit],cwd=ROOT,timeout=60,check=True)
    with zipfile.ZipFile(source) as z:assert z.testzip() is None
    digest=pkg.digest(source);Path(str(source)+'.sha256').write_text(digest+'  '+source.name+'\n',encoding='ascii')
    write(LOGS/'source-audit.json',{'source':str(source),'bytes':source.stat().st_size,'sha256':digest,'sourceArchiveCommit':commit,'compiledCodeCommit':manifest['baseCommit'],'nonProductChanges':changed,'productCodeEqual':True,'completeDependencySource':False,'sdkRuntimeModelsIncluded':False})
    print('SOURCE ZIP PASS',source,digest,commit,flush=True)
else:raise SystemExit('archive or source')
