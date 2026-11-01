"""Audit the tested local integration, preserved checkouts and native UI logs."""
from pathlib import Path
import hashlib,json,re,subprocess
B=Path('E:/项目/Veyra');T='capture-audio-main-20261025';R=Path(__file__).resolve().parents[2];O=B/'reports'/T;L=B/'logs'/T;S=B/'archives'/T
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args])
assert all(x['exit']==0 for x in json.loads((O/'contracts.json').read_text('utf8')))
assert all(x['exit']==0 for x in json.loads((O/'capture-final.json').read_text('utf8')))
assert json.loads((O/'nr-switch.json').read_text('utf8'))['exit']==0
assert json.loads((O/'integrated-export.json').read_text('utf8'))['passed']
rows=[]
for mode in ['software-final','gpu-final','gpu-node']:
    text=(L/('native-'+mode+'-console.log')).read_text('utf8',errors='replace')
    entered=text.count('[ui-fullscreen] enabled=true');left=text.count('[ui-fullscreen] enabled=false')
    assert entered>=1 and entered==left,(mode,entered,left)
    assert not re.search(r'\[ERROR\]|ReferenceError:|TypeError:|Cannot assign to non-existent',text),mode
    if mode=='gpu-node':assert 'mode=node' in text and '[ui-window] maximized=true' in text and 'locked=true shortcut=Ctrl+L' in text
    if mode.startswith('gpu'):assert 'interface=GPU' in text
    else:assert 'software UI:' in text
    rows.append(dict(mode=mode,entered=entered,left=left,log=str(L/('native-'+mode+'-console.log'))))
(O/'native-ui.json').write_text(json.dumps(rows,ensure_ascii=False,indent=1),'utf8')
before=json.loads((S/'worktrees-before.json').read_text('utf8'));preserved=[]
for row in before:
    p=Path(row['worktree'])
    if p.resolve()==R.resolve():continue
    assert git(p,'status','--porcelain=v1','-z').decode('utf8')==row['status'],str(p)
    assert git(p,'rev-parse','HEAD').decode().strip()==row['HEAD'],str(p)
    preserved.append(str(p))
fullscreen=Path('E:/项目/Veyra/worktrees/main-merge-20261002')
for name,digest in json.loads((S/'fullscreen-hashes.json').read_text('utf8')).items():assert sha(fullscreen/name)==digest,name
refs=dict(line.split(' ',1)[::-1] for line in (S/'refs-before.txt').read_text('utf8').splitlines())
now=dict(line.split(' ',1)[::-1] for line in git(R,'show-ref').decode().splitlines())
changed=[name for name,value in refs.items() if now.get(name)!=value]
assert set(changed)<=set(['refs/heads/main','refs/heads/codex/capture-audio-main-20261025']),changed
source=Path('E:/wechat/xwechat_files/wxid_kcmlgkgv70mv22_d585/msg/file/2026-10/veyra-qml(1).rar')
assert sha(source)==sha(S/'user-log.rar')=='809f6301dfd05977a3e8668728897a3bbf2ac0d05b967dece770a3f48b834fcd'
head=git(R,'rev-parse','HEAD').decode().strip()
result=dict(sourceCommit=head,mainCommit=git(R,'rev-parse','main').decode().strip(),otherWorktreesPreserved=len(preserved),originalFullscreenFilesPreserved=True,changedRefs=changed,testsPassed=True,nativeUi=rows,cv710HardwareRetested=False,pushed=False,published=False)
(O/'integration-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False))
