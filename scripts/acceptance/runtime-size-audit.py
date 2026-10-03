"""Read-only dependency/asset audit. No runtime/model is rewritten."""
import gc, hashlib, json, re, subprocess, sys, zipfile
from pathlib import Path
import pefile

ROOT=Path(__file__).resolve().parents[2]; BASE=Path('E:/项目/Veyra'); TASK='runtime-size-20261004'
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
OLD=BASE/'test-packages/vfg-integration-20261003/Veyra-2.0.2-vfg-20261003-win64-portable'
ARCHIVE=Path(str(OLD)+'.zip'); LOGS=BASE/'logs'/TASK; LOGS.mkdir(parents=True,exist_ok=True)
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()
results={}
for p in sorted((OLD/'runtime/nvidia-vfg').glob('*.dll')):
    pe=pefile.PE(str(p),fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
    direct=[x.dll.decode() for x in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])]
    delay=[x.dll.decode() for x in getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[])]
    raw=p.read_bytes()
    names=sorted(set(x.decode() for x in re.findall(rb'[A-Za-z0-9_-]{3,}\.dll',raw)))
    results[p.name]={'bytes':p.stat().st_size,'sha256':digest(p),'imports':direct,'delayImports':delay,'dllNameStrings':names}
    pe.close(); del pe,raw; gc.collect()
with zipfile.ZipFile(ARCHIVE) as z:
    stats=[]
    for kind in ('runtime/nvidia-vfg/','runtime/amd-nr/'):
        files=[i for i in z.infolist() if '/'+kind in i.filename and not i.is_dir()]
        stats.append({'kind':kind,'files':len(files),'rawBytes':sum(i.file_size for i in files),'compressedBytes':sum(i.compress_size for i in files),
                      'largestCompressed':[{'path':i.filename.split('/',1)[1],'raw':i.file_size,'compressed':i.compress_size} for i in sorted(files,key=lambda i:i.compress_size,reverse=True)[:15]]})
amd=OLD/'runtime/amd-nr'; assets=[]
for p in sorted(amd.rglob('*')):
    if p.is_file():assets.append({'path':p.relative_to(amd).as_posix(),'bytes':p.stat().st_size,'sha256':digest(p)})
duplicate={}
for r in assets:duplicate.setdefault((r['sha256'],r['bytes']),[]).append(r['path'])
duplicates=[{'sha256':h,'bytes':n,'paths':paths} for (h,n),paths in duplicate.items() if len(paths)>1 and n>1024]
source=BASE/'deps/lmxxf-nr-20261003/src'
references={}
for term in ('normalized-output.f32','noise.f32','noise.f16'):
    r=subprocess.run(['rg','-n','-F',term,str(source)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert r.returncode in (0,1),r.stderr
    references[term]=r.stdout.decode('utf8',errors='replace').splitlines()
record={'oldArchiveBytes':ARCHIVE.stat().st_size,'stats':stats,'vfgDependencies':results,'amdFiles':assets,'amdDuplicatePayloads':duplicates,'amdRequiredAssetReferences':references}
(LOGS/'dependency-asset-audit.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'oldArchiveBytes':record['oldArchiveBytes'],'stats':stats,'vfgImports':{n:{k:v for k,v in r.items() if k in ('imports','delayImports','dllNameStrings')} for n,r in results.items() if not n.startswith('npp')},'amdDuplicates':duplicates,'references':references},ensure_ascii=False,indent=2))
