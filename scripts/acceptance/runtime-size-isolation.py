"""Small local EXE patch and read-only isolation audit; no full runtime duplication."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra')
TASK='runtime-size-20261004'
LOGS=BASE/'logs'/TASK
OUT=BASE/'test-packages'/TASK
REFERENCE=BASE/'test-packages/vfg-integration-20261003/Veyra-2.0.2-vfg-20261003-win64-portable'
DESKTOP=Path('C:/Users/123/Desktop/Veyra DLSS Video Player')
START=BASE/'archives/field-upgrade-20261003-start'
EXPECTED_BASE='be4da2ff173aa74de8e2e1d5ecc73d40a23584ba8898ffd534e636a8793b3d6b'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd,stderr=subprocess.PIPE)
def normalized(value):return value.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
LOGS.mkdir(parents=True,exist_ok=True)
if sys.argv[1]=='audit':
    start=json.loads((START/'manifest.json').read_text(encoding='utf-8-sig'))
    untracked=[row for row in start['files'] if row['path'].replace('\\','/').startswith('desktop-untracked/')]
    isolation={
        'desktopStatusNormalizedEqual':normalized(git('status','--porcelain','--branch',cwd=DESKTOP))==normalized((START/'desktop-status.txt').read_bytes()),
        'workingPatchBytesEqual':git('diff','--binary',cwd=DESKTOP)==(START/'desktop-working.patch').read_bytes(),
        'indexPatchBytesEqual':git('diff','--cached','--binary',cwd=DESKTOP)==(START/'desktop-index.patch').read_bytes(),
        'untrackedSourceHashesEqual':all(digest(DESKTOP/row['path'].replace('\\','/').split('/',1)[1]).upper()==row['sha256'].upper() for row in untracked),
        'mainPreserved':git('rev-parse','main').decode().strip()=='66cd3e50590766e5a654528ab61e491dc4d30c83',
    }
    manifest=json.loads((REFERENCE/'package-manifest.json').read_text(encoding='utf-8-sig'))
    rows=manifest['files']
    isolation['basePortablePayloadHashesEqual']=all(digest(REFERENCE/row['path']).lower()==row['sha256'].lower() for row in rows)
    isolation['baseExeHashEqual']=digest(REFERENCE/'veyra_qml_ui.exe')==EXPECTED_BASE
    LOGS.joinpath('source-isolation.json').write_text(json.dumps(isolation,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(isolation,indent=2))
    assert all(isolation.values()),'Isolation audit failed'
    print('ISOLATION PASS',len(untracked),'untracked sources;',len(rows),'original payloads unchanged')
else:raise SystemExit('Use audit')
