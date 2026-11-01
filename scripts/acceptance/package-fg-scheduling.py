"""Seal verified local scheduling update; no runtimes, logs or user configuration."""
from pathlib import Path
import json,hashlib,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='fg-scheduling-repair-20261024';R=Path(__file__).resolve().parents[2];O=B/'reports'/T
D=B/'build/claude-screen-recording-20261022';P=B/'test-packages'/T/'Veyra-2.1.0-fg1-NVIDIA-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit reviewed source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert json.loads((O/'native.json').read_text('utf8'))['passed']
comparisons=json.loads((O/'capture-comparison.json').read_text('utf8'));assert len(comparisons)==4 and all(x['actualFormat'].startswith('2560x1440p60 NV12') for x in comparisons)
assert all(x['exit']==0 for x in json.loads((O/'contracts.json').read_text('utf8')))
assert not P.exists();P.mkdir(parents=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
assert sha(P/'veyra_qml_ui.exe')==json.loads((O/'native.json').read_text('utf8'))['exeSha256']
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(R/folder))
for name in ['FG_SCHEDULING_REPAIR_PLAN_2026-10-24.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md','NR_MANUAL_SCALE_REPORT_2026-10-24.md']:
    copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
note='''# Veyra 2.1.0-fg1 NVIDIA 本地测试更新

需要已有完整2.1.0 NVIDIA便携包。先关闭播放器，将本目录文件复制到原包根目录（veyra_qml_ui.exe所在目录），覆盖同名文件，再启动。
本更新只含程序、QML、翻译和说明，不含DLL/模型/SDK/日志/用户配置。原package-manifest描述完整底包；本更新的程序/UI身份以LOCAL_UPDATE.json为准。

- 将显示器刷新率及NVIDIA G-SYNC/VRR状态查询移到后台，避免每两秒同步驱动查询使当前采集帧错过补帧期限。播放线程只取完成结果。
- 保留原补帧预算、期限、跳帧及告警判定，不积压帧，不保证负载真正超预算时仍能补帧。
- 同时保留nr1新增的每层NR手动25%–100%内部比例滑条，以及此前旋转/录制修复；Lecram仍暂停。
- 已在本机KUHAIMI G50真采集2560x1440p60 NV12、NR/SR关闭、VFG2X下做旧nr1与fg1对照，含原60和60->30两组；详细计数见随包修复报告。
- 已验证4K30文件VFG2X/4X、跟随显示器/VRR、暂停恢复、关闭重开和针对性CPU回归。其它采集卡/其它FG后端/重NR负载/HDR/长期与光学延迟不由本次短测证明。

版本标识2.1.0-fg1，PE数值版本2.1.0.0。本地隔离分支交付，未合main、未push或公开发布。
'''
(P/'README-FG1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(x['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for x in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-fg1-local-update',base='Complete 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(R),localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
