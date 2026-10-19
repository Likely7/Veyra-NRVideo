"""Promote the user-approved ZIP bytes unchanged; no build/repack operations."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,zipfile
R=Path(__file__).resolve().parents[2];B=Path('E:/项目/Veyra');TASK='release-2.1.0-20261019'
P=B/'test-packages/bilibili-2.1.0-20261019';OUT=B/'releases'/TASK;REPORT=B/'reports'/TASK;ARCH=B/'archives'/TASK
for d in (OUT,REPORT,ARCH):d.mkdir(parents=True,exist_ok=True)
def git(*args):return subprocess.check_output(['git',*args],cwd=R,text=True,encoding='utf8').strip()
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def gh(*args):return json.loads(subprocess.check_output(['gh',*args],cwd=R,text=True,encoding='utf8'))
parser=argparse.ArgumentParser();parser.add_argument('mode',choices=('prepare','verify','remote'));a=parser.parse_args()
delivery=json.loads((P/'DELIVERY.json').read_text('utf8'))
items=[(P/Path(x['archive']).name,OUT/f"Veyra-2.1.0-{x['vendor']}-win64-portable.zip",x['sha256']) for x in delivery['packages']]
items.extend([(P/'Veyra-2.1.0-test-source.zip',OUT/'Veyra-2.1.0-source.zip',delivery['source']['sha256']),
              (P/'lmxxf-044-corresponding-source.zip',OUT/'lmxxf-044-corresponding-source.zip',delivery['amdNr044Source']['sha256'])])
QR=R/'docs/images/2.1.0/community-group.png'
if a.mode=='prepare':
    (ARCH/'refs-before.txt').write_text(git('show-ref'),'utf8');(ARCH/'worktrees-before.txt').write_text(git('worktree','list','--porcelain'),'utf8')
    (ARCH/'delivery-before.json').write_bytes((P/'DELIVERY.json').read_bytes())
    source=Path('C:/Users/123/AppData/Local/Temp/codex-clipboard-7b6a44f7-272b-4182-aa87-fafa74d98a8a.png')
    QR.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,QR);assert sha(source)==sha(QR)
    for src,dst,h in items:
        assert sha(src)==h,(src,'input digest')
        if not dst.exists():os.link(src,dst)
        assert sha(dst)==h,(dst,'promoted digest')
    logs=git('log','--reverse','--format=%H%x09%s','v2.0.9..d91ec9f').splitlines()
    paths=git('diff','--name-status','v2.0.9','d91ec9f').splitlines()
    nonmerge=git('rev-list','--no-merges','--count','v2.0.9..d91ec9f')
    index=['# 2.0.9 → 2.1.0 全部变更索引','',f'范围：v2.0.9（08832f6）至已验main d91ec9f；共{len(logs)}提交（非merge {nonmerge}），{len(paths)}变更路径。包含双方提交、云端PR、合并与验收文档；下列提交标题不是逐条新功能声明。产品总结见 RELEASE_NOTES_2.1.0.md。Lecram实验不在本范围。','','## 全部提交','']
    index.extend(f'- `{x.split(chr(9))[0][:12]}` {x.split(chr(9),1)[1]}' for x in logs)
    index.extend(['','## 全部路径（A新增 / M修改 / D删除）','','```text',*paths,'```',''])
    (R/'docs/RELEASE_2.1.0_CHANGE_INDEX.md').write_text('\n'.join(index),'utf8')
    support=(R/'docs/RELEASE_SUPPORT.md').read_text('utf8').replace('v2.0.7/docs/images/2.0.7/community-group.png','v2.1.0/docs/images/2.1.0/community-group.png').replace('2026-10-19','2026-10-26')
    (R/'docs/RELEASE_SUPPORT.md').write_text(support,'utf8')
    notes=R/'docs/RELEASE_NOTES_2.1.0.md';body=notes.read_text('utf8')
    if '## 支持与反馈 / Support & feedback' not in body:notes.write_text(body+'\n'+support,'utf8')
    source_body='''# Veyra 2.1.0 · 完整对应源码 / Corresponding sources

[返回两个软件下载包](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0) · [构建与完整依赖清单](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/BUILD_2.1.0.md)

本页只存源码，不是另一个播放器包，不设latest。应用源码沿用用户已验ZIP，字节不变，仅去掉外部文件名的-test。

- Veyra-2.1.0-source.zip：精确源码66b321051bef9343fa2bcc6556a53a551ccc809c，2187文件，SHA256 `d3ed0860cebef310eeceb5fafd790b553f78cecdf2d393e9f4c4d6ca87b32559`。
- lmxxf-044-corresponding-source.zip：AMD Small/Original两个本地模块的完整构建输入、固定上游源码、补丁、配方及许可证，SHA256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`。

还需要未改变的既有材料（固定资产，不重复上传）：

- [FFmpeg slices256/dav1d/zlib源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。
- [独立去隔行LGPL模块源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip)，SHA256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`。
- [依赖与重链接材料](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。

Veyra GPL-3.0，Chiaki组合适用AGPL-3.0及上游例外；专有SDK/运行库/模型不进源码Git，NVIDIA衍生权重不因外围MIT源码变成MIT。原包许可证/运行清单保留。发布标签新增的是发行文档、README、群图与发布校验脚本，不更改已验应用输入；GitHub自动生成仓库源码不等于全部依赖材料。

English: These are the exact tested application sources and the corresponding sources for both AMD0.44 modules. Rebuild instructions, retained dependency/relinking sources and licenses are linked above. The main release has just the two GPU application packages. This source archive is not latest.
'''
    (R/'docs/RELEASE_SOURCE_2.1.0.md').write_text(source_body+'\n'+support,'utf8')
    print('prepared unchanged archives and release index; QR SHA256',sha(QR))
if a.mode in ('prepare','verify'):
    rows=[]
    for src,dst,h in items:
        assert sha(src)==sha(dst)==h
        with zipfile.ZipFile(dst) as z:
            assert z.testzip() is None
            names=z.namelist();exe=[n for n in names if n.endswith('/veyra_qml_ui.exe')]
            if exe:assert len(exe)==1 and hashlib.sha256(z.read(exe[0])).hexdigest()==delivery['packages'][0]['exeSha256']
        rows.append(dict(name=dst.name,bytes=dst.stat().st_size,sha256=h,files=len(names)))
    notes=(R/'docs/RELEASE_NOTES_2.1.0.md').read_text('utf8')
    for word in ('0.44','468','PR57','PR63','PR64','#48','0.93','1.42','2.1.0-test','10-26','c9aREyMj8','ko-fi.com/likely7','qcnv4i7tnd7l'):assert word in notes,word
    assert notes.count('width="220"')==2
    assert 'renodx' not in git('diff','--name-only','d91ec9f').lower()
    bad=[n for n in git('diff','--name-only','d91ec9f').splitlines() if n.startswith(('src/','include/','qml/','shaders/','apps/')) or n=='CMakeLists.txt']
    assert not bad,bad
    (REPORT/'local.json').write_text(json.dumps(dict(passed=True,archives=rows,qrSha256=sha(QR),applicationSource=delivery['mainCommit'],rebuild=False,repack=False),ensure_ascii=False,indent=2),'utf8')
    print('local verification passed',json.dumps(rows))
if a.mode=='remote':
    results=[]
    for tag,expected in [('v2.1.0',items[:2]),('v2.1.0-sources',items[2:])]:
        r=gh('release','view',tag,'--repo','Likely7/Veyra-NRVideo','--json','tagName,isDraft,isPrerelease,url,assets,body,targetCommitish')
        assets={x['name']:x for x in r['assets']};assert set(assets)=={dst.name for _,dst,_ in expected}
        for _,dst,h in expected:
            x=assets[dst.name];assert x['state']=='uploaded' and x['size']==dst.stat().st_size and x['digest']=='sha256:'+h
        for s in ('v2.1.0/docs/images/2.1.0/community-group.png','donate-wechat.jpg','c9aREyMj8','ko-fi.com/likely7','qcnv4i7tnd7l'):assert s in r['body']
        assert r['body'].count('width="220"')==2
        results.append(r)
    (REPORT/'remote.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf8')
    print('remote digests, sizes, exact asset sets and support blocks verified')
