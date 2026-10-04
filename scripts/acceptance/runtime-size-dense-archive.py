"""Use a larger lossless dictionary to reuse shared NR model blocks."""
import importlib.util, json, shutil, subprocess, sys, time
from pathlib import Path
import py7zr
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
STAGE=BASE/'test-packages'/TASK/'Veyra-2.0.2-slim-20261004-win64-portable'
LOGS=BASE/'logs'/TASK
spec=importlib.util.spec_from_file_location('qml_package',ROOT/'scripts/package-qml-release.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'))
files=sorted(p for p in STAGE.rglob('*') if p.is_file());expected={r['path'] for r in manifest['files']}|{'package-manifest.json'}
assert {p.relative_to(STAGE).as_posix() for p in files}==expected
archive=Path(str(STAGE)+'-dense.7z');assert not archive.exists()
start=time.monotonic()
with py7zr.SevenZipFile(archive,'w',filters=[{'id':py7zr.FILTER_LZMA2,'preset':5,'dict_size':256*1024*1024}]) as z:
    for i,p in enumerate(files):
        assert not p.is_symlink();z.write(p,STAGE.name+'/'+p.relative_to(STAGE).as_posix())
        if i and i%250==0:print('256M DICTIONARY FILES',i,'/',len(files),flush=True)
print('DENSE WRITTEN; FRESH EXTRACT/HASH NEXT',archive.stat().st_size,flush=True)
verify=BASE/'verify'/TASK/'dense';verify.mkdir(parents=True,exist_ok=False)
with py7zr.SevenZipFile(archive,'r') as z:
    names=z.getnames();assert len(names)==len(expected) and set(names)=={STAGE.name+'/'+r for r in expected}
    for name in names:pkg.relative(name)
    z.extractall(path=verify)
app=verify/STAGE.name
for row in manifest['files']:
    p=app/row['path'];assert p.is_file() and not p.is_symlink() and p.stat().st_size==row['size'] and pkg.digest(p)==row['sha256'],row['path']
assert pkg.digest(app/'package-manifest.json')==pkg.digest(STAGE/'package-manifest.json')
result={'archive':str(archive),'sha256':pkg.digest(archive),'bytes':archive.stat().st_size,'method':'7z-solid-LZMA2-256MiB','dictionaryBytes':256*1024*1024,'decoderMemoryNote':'Requires about 256MiB dictionary plus decoder buffers while extracting; no runtime memory change. ZIP fallback provided.',
        'seconds':time.monotonic()-start,'verifiedFiles':len(manifest['files']),'status':'pass','extractedApp':str(app)}
(LOGS/'dense-archive-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
Path(str(archive)+'.sha256').write_text(result['sha256']+'  '+archive.name+'\n',encoding='ascii')
print('DENSE FRESH EXTRACTION ALL HASHES PASS',json.dumps(result,ensure_ascii=False),flush=True)
