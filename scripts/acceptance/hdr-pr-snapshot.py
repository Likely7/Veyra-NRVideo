"""Save immutable PR source as review data, without checking out or executing it."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path('E:/项目/Veyra/downloads/minimal-edge-hdr-review-20261004')
heads = {13: 'f08934b8a535bb0ac085cd8f5689b9029b416726', 14: '57f2541b3173b5285661ddf6c448ffb2722f6433'}
records = []
for number, head in heads.items():
    metadata = json.loads((OUT / f'pr-{number}.json').read_text(encoding='utf-8-sig'))
    assert metadata['headRefOid'] == head
    assert subprocess.check_output(['git','rev-parse',f'refs/veyra-review/20261004/pr{number}'],cwd=ROOT).decode().strip() == head
    target = OUT / f'pr-{number}'
    target.mkdir(exist_ok=False)
    rows = []
    for row in metadata['files']:
        path = row['path']
        destination = target / path
        destination.resolve().relative_to(target.resolve())
        destination.parent.mkdir(parents=True,exist_ok=True)
        blob = subprocess.check_output(['git','show',f'{head}:{path}'],cwd=ROOT)
        destination.write_bytes(blob)
        rows.append({'path':path,'sha256':hashlib.sha256(blob).hexdigest(),'bytes':len(blob)})
    diff = subprocess.check_output(['git','diff','--binary',metadata['baseRefOid'],head],cwd=ROOT)
    (OUT / f'pr-{number}-pinned.diff').write_bytes(diff)
    records.append({'number':number,'head':head,'base':metadata['baseRefOid'],'files':rows,'diffSha256':hashlib.sha256(diff).hexdigest(),'executedContributorCode':False})
    print('PINNED PR',number,head,len(rows),'files')
(OUT/'snapshot-manifest.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
