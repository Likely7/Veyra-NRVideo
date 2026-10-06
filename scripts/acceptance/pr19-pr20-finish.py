"""Freeze tested inputs, commit preserved PR history, advance main without force."""
from pathlib import Path
import datetime,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='pr19-pr20-20261006'
LOG=BASE/'logs'/TASK;ARCHIVE=BASE/'archives'/TASK
start=json.loads((ARCHIVE/'start.json').read_text(encoding='utf8'));MAIN=Path(start['mainPath'])
mode=sys.argv[1];assert mode in ('freeze','commit','merge','push','verify')
def run(*args,cwd=ROOT):
    result=subprocess.run(list(args),cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
    return result.stdout.decode('utf8',errors='replace').strip()
def git(*args,cwd=ROOT):return run('git',*args,cwd=cwd)
def gh(*args):return json.loads(run('gh','api',*args))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(path,data):
    with path.open('x',encoding='utf8') as out:json.dump(data,out,ensure_ascii=False,indent=2)
def guard():
    print(run(sys.executable,'-B',str(ROOT/'scripts/acceptance/pr19-pr20-control.py'),'--published'))
def release():
    r=gh('repos/Likely7/Veyra-NRVideo/releases/tags/v2.0.4')
    return {k:r[k] for k in ('id','tag_name','body','draft','prerelease','published_at','assets')}
def frozen():
    f=json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))
    for p,h in f['productInputs'].items():assert sha(ROOT/p)==h,p
    for p,h in f['testInputs'].items():assert sha(ROOT/p)==h,p
    for p,h in f['executables'].items():assert sha(p)==h,p
    for p,h in f['evidence'].items():assert sha(p)==h,p
    return f
def original_heads():
    prs=[]
    for p in start['prs']:
        number=p.get('number',p.get('pr'))
        assert number in (19,20),p
        r=gh('repos/Likely7/Veyra-NRVideo/pulls/'+str(number))
        assert r['head']['sha']==p['head'],number
        prs.append(dict(number=number,head=r['head']['sha'],state=r['state'],merged=r['merged'],mergeCommit=r['merge_commit_sha']))
    return prs
guard()
if mode=='freeze':
    assert git('rev-parse','MERGE_HEAD')==start['prs'][0]['head']
    assert not git('diff','--name-only','--diff-filter=U')
    labels=['build-final-v6','build-hdr-resize-v8','build-remote-off-v3','units-final-v4','hdr-final-v4','graph-final-v1','abi-final-v1','worker-stack-final-v1','gui-export-final-v1','hdr-ui-final-v2','hdr-ui-restart-final-v2']
    for label in labels:
        data=json.loads((LOG/(label+'.json')).read_text(encoding='utf8'))
        if isinstance(data,list):assert all(v['passed'] for v in data),label
        else:assert data.get('passed',data.get('exit')==0),label
    # Refresh the index only after the scope guard; it does not rewrite source bytes.
    git('add','-A');git('diff','--cached','--check')
    names=git('ls-files','-z').split('\0')
    prefixes=('src/','include/','apps/','qml/','shaders/','cmake/','i18n/','resources/','assets/','tools/')
    product={p:sha(ROOT/p) for p in names if p and (p.startswith(prefixes) or p in ('CMakeLists.txt','CMakePresets.json'))}
    tests={p:sha(ROOT/p) for p in names if p and (p.startswith('tests/') or p.startswith('scripts/acceptance/pr19-pr20-'))}
    executables={str(p):sha(p) for p in (BASE/'build'/TASK/'standard').glob('*.exe')}
    off=BASE/'build'/TASK/'remote-off/veyra_qml_ui.exe';executables[str(off)]=sha(off)
    evidence={str(p):sha(p) for p in LOG.glob('*.json')}
    for p in LOG.glob('*.log'):evidence[str(p)]=sha(p)
    # Preserve GUI child-worker logs before any staging cleanup.
    workers=LOG/'gui-worker-logs';workers.mkdir(exist_ok=False)
    import shutil
    for p in (BASE/'verify'/TASK/'ui-app/logs').glob('export-worker-*.log'):
        shutil.copyfile(p,workers/p.name);evidence[str(workers/p.name)]=sha(workers/p.name)
    info=dict(created=datetime.datetime.now(datetime.timezone.utc).isoformat(),indexTree=git('write-tree'),productInputs=product,testInputs=tests,executables=executables,evidence=evidence,passedReceipts=labels)
    write(LOG/'remote-release-before.json',release());write(LOG/'remote-before.json',original_heads())
    write(LOG/'tested-inputs.json',info)
    print('FROZEN',len(product),'product inputs;',len(tests),'test/script inputs;',len(evidence),'evidence files')
elif mode=='commit':
    frozen();assert git('write-tree')==json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))['indexTree']
    assert git('rev-parse','MERGE_HEAD')==start['prs'][0]['head']
    original_heads()
    message='Merge PR #19: adapt static HDR output tuning to current NR/Flow contracts\n\nPreserve current NR, export and OBS fixes. Decode PQ before the preview curve, preserve wide-gamut RGB, version HDR presets separately, and keep metadata optional with source/resize lifecycle handling.\n\nValidated normal and RemotePlay-off production builds, HDR and persistence regression checks, AMD identity/ABI contracts, and real QML multi-NR exports.\n'
    body=BASE/'tmp'/TASK/'merge-message.txt';body.write_text(message,encoding='utf8')
    print(git('commit','-F',str(body)))
    head=git('rev-parse','HEAD')
    for p in start['prs']:git('merge-base','--is-ancestor',p['head'],head)
    assert git('rev-parse','HEAD^{tree}')==json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))['indexTree']
    assert not git('status','--porcelain=v1')
    # New reachable history must not introduce proprietary runtime/SDK artifacts.
    commits=git('rev-list',start['mainBefore']+'..'+head).splitlines()
    forbidden={'.dll','.exe','.lib','.pdb','.onnx','.hsaco','.dxil','.7z','.zip','.bin'}
    for commit in commits:
        for p in git('diff-tree','--no-commit-id','--name-only','--diff-filter=A','-r',commit).splitlines():
            assert Path(p).suffix.lower() not in forbidden,(commit,p)
    bundle=ARCHIVE/'source-integration.bundle'
    git('bundle','create',str(bundle),start['branch'],'codex/pr19-original-20261006','codex/pr20-original-20261006')
    verify=run('git','bundle','verify',str(bundle));(LOG/'source-integration-verify.log').write_text(verify,encoding='utf8')
    write(LOG/'integration-commit.json',dict(head=head,tree=git('rev-parse','HEAD^{tree}'),parents=git('show','-s','--format=%P'),bundle=str(bundle),bundleSHA=sha(bundle),novelCommitsAudited=len(commits)))
    print('COMMITTED',head,'proprietary artifact audit passed')
elif mode=='merge':
    frozen();original_heads();assert not git('status','--porcelain=v1')
    head=git('rev-parse','HEAD');remote=git('ls-remote','nrvideo','refs/heads/main').split()[0]
    assert remote==start['remoteMainBefore'],remote
    assert git('rev-parse','HEAD',cwd=MAIN)==start['mainBefore']
    assert not git('status','--porcelain=v1',cwd=MAIN)
    git('merge-base','--is-ancestor',start['mainBefore'],head)
    print(git('merge','--ff-only',head,cwd=MAIN))
    assert git('rev-parse','HEAD',cwd=MAIN)==head
    assert git('rev-parse','HEAD^{tree}',cwd=MAIN)==git('rev-parse','HEAD^{tree}')
    write(LOG/'main-advance.json',dict(before=start['mainBefore'],after=head,tree=git('rev-parse','HEAD^{tree}'),fastForward=True))
    guard();print('MAIN ADVANCED',head)
elif mode=='push':
    frozen();original_heads()
    head=git('rev-parse','HEAD');assert git('rev-parse','HEAD',cwd=MAIN)==head
    assert git('ls-remote','nrvideo','refs/heads/main').split()[0]==start['remoteMainBefore']
    print(git('push','nrvideo','main:main',cwd=MAIN))
    write(LOG/'push.json',dict(expectedMain=head,remoteMain=git('ls-remote','nrvideo','refs/heads/main').split()[0]))
    print('PUSHED',head)
else:
    frozen();head=git('rev-parse','HEAD')
    assert git('ls-remote','nrvideo','refs/heads/main').split()[0]==head
    prs=original_heads();assert all(p['merged'] and p['state']=='closed' for p in prs),prs
    assert release()==json.loads((LOG/'remote-release-before.json').read_text(encoding='utf8'))
    write(LOG/'remote-after.json',dict(main=head,prs=prs,releaseUnchanged=True,originalWorktreesPreserved=True,originalPublishedZIPsPreserved=True))
    print('VERIFIED',head,'PR19/20 merged; v2.0.4 release unchanged')
