"""Publish only the audited v2.0.6 assets, then verify GitHub server digests."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,urllib.request,datetime
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
LOG=BASE/'logs'/TASK;OUT=BASE/'releases'/TASK;REPO='Likely7/Veyra-NRVideo';TAG='v2.0.6'
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def command(*args):return subprocess.check_output(list(args),stderr=subprocess.PIPE).decode('utf8')
def api(path):return json.loads(command('gh','api',path))
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')],check=True)
phase=sys.argv[1];assets=json.loads((LOG/'assets.json').read_text(encoding='utf8'))
merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));head=merge['mainAfter']
assert command('git','-C',str(ROOT),'status','--porcelain').strip()==''
assert command('git','-C',str(ROOT),'rev-parse','HEAD').strip()==merge['sourceCommit']
for row in assets:assert Path(row['path']).stat().st_size==row['bytes'] and sha(row['path'])==row['sha256']
bodyfile=OUT/'RELEASE_BODY.md'
if not bodyfile.exists():
    text=(ROOT/'docs/RELEASE_NOTES_2.0.6.md').read_text(encoding='utf8')
    text=re.sub(r'\]\((?!https?://)([^)]+)\)',lambda m:'](https://github.com/'+REPO+'/blob/'+TAG+'/docs/'+m[1]+')',text)
    text+='\n\n---\n\n'+(ROOT/'docs/RELEASE_SUPPORT.md').read_text(encoding='utf8')
    bodyfile.write_text(text,encoding='utf8')
body=bodyfile.read_text(encoding='utf8')
assert body.count('width="220"')==2 and 'https://discord.gg/c9aREyMj8' in body and 'https://ko-fi.com/likely7' in body
if phase=='push':
    for name in ('cold-verify-results.json','stage-AMD.json','stage-NVIDIA.json','source-audit.json'):assert (LOG/name).is_file(),name
    cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'));assert len(cold)==2 and all(r['passed'] for r in cold)
    before=api('repos/'+REPO+'/releases/tags/v2.0.5');save(LOG/'previous-release-before.json',before)
    assert command('git','-C',str(ROOT),'ls-remote','--heads','nrvideo','refs/heads/main').split()[0]=='af5bfc3a66afce3047868229a3070a91c0fc9c22'
    assert not command('git','-C',str(ROOT),'ls-remote','--tags','nrvideo','refs/tags/'+TAG).strip()
    subprocess.run(['git','-C',str(ROOT),'tag','-a',TAG,head,'-m','Veyra 2.0.6 stable application release'],check=True)
    subprocess.run(['git','-C',str(ROOT),'push','--atomic','nrvideo','main:main','refs/tags/'+TAG],check=True)
    save(LOG/'push.json',dict(mainCommit=head,sourceCommit=merge['sourceCommit'],tag=TAG,passed=True))
elif phase=='draft':
    assert (LOG/'push.json').is_file()
    subprocess.run(['gh','release','create',TAG,'--repo',REPO,'--draft','--verify-tag','--title',
        'Veyra 2.0.6 · VFG performance, AMD NR & Blackmagic capture','--notes-file',str(bodyfile)],check=True)
    save(LOG/'draft-created.json',api('repos/'+REPO+'/releases/tags/'+TAG))
    subprocess.run(['gh','release','upload',TAG,'--repo',REPO,*[r['path'] for r in assets]],check=True,timeout=1800)
elif phase in ('verify-draft','verify-public'):
    release=api('repos/'+REPO+'/releases/tags/'+TAG)
    assert release['draft']==(phase=='verify-draft') and not release['prerelease']
    assert release['body'].replace('\r\n','\n')==body.replace('\r\n','\n')
    assert len(release['assets'])==len(assets)==5
    expected={r['name']:r for r in assets}
    for r in release['assets']:
        local=expected.pop(r['name']);assert r['size']==local['bytes'] and r['state']=='uploaded'
        assert r.get('digest')=='sha256:'+local['sha256'],(r['name'],r.get('digest'))
    assert not expected
    ref=api('repos/'+REPO+'/git/ref/tags/'+TAG)['object']
    if ref['type']=='tag':ref=api('repos/'+REPO+'/git/tags/'+ref['sha'])['object']
    assert ref['type']=='commit' and ref['sha']==head
    finishpath=LOG/'documentation-close.json'
    expected_main=json.loads(finishpath.read_text(encoding='utf8'))['mainAfter'] if finishpath.exists() else head
    assert api('repos/'+REPO+'/git/ref/heads/main')['object']['sha']==expected_main
    previous=api('repos/'+REPO+'/releases/tags/v2.0.5')
    old=json.loads((LOG/'previous-release-before.json').read_text(encoding='utf8'))
    for key in ('id','tag_name','body','published_at','assets'):assert previous[key]==old[key],key
    result=dict(passed=True,phase=phase,releaseId=release['id'],url=release['html_url'],mainCommit=expected_main,releaseMainCommit=head,
        sourceCommit=merge['sourceCommit'],tagCommit=ref['sha'],publishedAt=release['published_at'],
        draft=release['draft'],prerelease=release['prerelease'],assets=release['assets'],bodySha256=sha(bodyfile),
        originalReleaseUnchanged=True,checkedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    if phase=='verify-public':
        latest=api('repos/'+REPO+'/releases/latest');assert latest['id']==release['id']
        destination=BASE/'verify'/TASK/('remote-final' if finishpath.exists() else 'remote');destination.mkdir(parents=True,exist_ok=False)
        subprocess.run(['gh','release','download',TAG,'--repo',REPO,'--pattern','SHA256SUMS.txt','--dir',str(destination)],check=True,timeout=120)
        assert sha(destination/'SHA256SUMS.txt')==sha(OUT/'SHA256SUMS.txt')
        urls=[('release',release['html_url'])]
        urls.extend((r['name'],r['browser_download_url']) for r in release['assets'])
        urls.extend(('support-image',u.replace('&amp;','&')) for u in re.findall(r'<img src="([^"]+)"[^>]+width="220"',body))
        urls.append(('ko-fi-button','https://storage.ko-fi.com/cdn/kofi5.png?v=6'))
        availability=[]
        for name,url in urls:
            req=urllib.request.Request(url,method='HEAD',headers={'User-Agent':'Veyra-release-verification/2.0.6'})
            with urllib.request.urlopen(req,timeout=60) as response:
                assert response.status==200,(name,response.status)
                availability.append(dict(name=name,url=url,status=response.status,contentType=response.headers.get('Content-Type')))
        qrurl='https://raw.githubusercontent.com/'+REPO+'/'+TAG+'/docs/images/2.0.6/community-group.png'
        with urllib.request.urlopen(qrurl,timeout=60) as response: qr=response.read()
        qrsha=hashlib.sha256(qr).hexdigest()
        assert qrsha=='5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd'
        result.update(latest=True,downloadedChecksumsIdentical=True,httpAvailability=availability,communityQrSha256=qrsha)
        save(LOG/'remote-public-release.json',release)
    save(LOG/(phase+'.json'),result);print('REMOTE VERIFY PASS',phase,len(release['assets']),'server SHA256 digests',flush=True)
elif phase=='publish':
    draft=json.loads((LOG/'verify-draft.json').read_text(encoding='utf8'));assert draft['passed'] and draft['draft']
    subprocess.run(['gh','release','edit',TAG,'--repo',REPO,'--draft=false','--prerelease=false','--latest'],check=True)
else:raise SystemExit('Unknown phase')
