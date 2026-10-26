"""Seal the common EXE/QML update without any runtime/model/configuration files."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='capture-audio-main-20261025';R=Path(__file__).resolve().parents[2];O=B/'reports'/T;D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';P=B/'test-packages'/T/'Veyra-2.1.0-int1-NVIDIA-AMD-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit the reviewed source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert subprocess.check_output(['git','-C',str(R),'rev-parse','main'],text=True).strip()==head,'merge local main first'
audit=json.loads((O/'integration-audit.json').read_text('utf8'));assert audit['sourceCommit']==head and audit['mainCommit']==head and audit['testsPassed']
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(D/'veyra_qml_ui.exe')==sha(A/'veyra_qml_ui.exe')==json.loads((O/'prepared.json').read_text('utf8'))['exeSha256']
assert not P.exists();P.mkdir(parents=True)
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():
            assert sha(A/folder/p.relative_to(R/folder))==sha(p),p
            copy(p,Path(folder)/p.relative_to(R/folder))
for name in ['CAPTURE_AUDIO_MAIN_REPORT_2026-10-25.md','EXPORT_NR_SCALE_REPORT_2026-10-25.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md']:copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
(P/'README-INT1.zh-CN.md').write_text('''# Veyra 2.1.0-int1 本地整合更新

需要已有完整2.1.0 NVIDIA或AMD便携包。先退出播放器，再把本目录内容复制到原包根目录（veyra_qml_ui.exe所在目录）覆盖同名文件。保留原包即可回退。本更新只有共同程序/UI/说明，不含任何DLL、模型、驱动、用户配置或日志，不是完整便携包。NVIDIA和AMD使用同一个EXE，原包各自的运行组件保持原样；原package-manifest仅代表底包，更新程序身份见LOCAL_UPDATE.json。

包含：录制/AMD超分关闭、90°旋转、NR25%–100%手动比例、后台VRR查询、NR视频导出固定/百分比尺寸、#67 NR关闭生命周期、全屏按钮/Esc/播放列表键归属、采集无时戳预备样本正常接收与诊断。

CV710修复的是已确认的接收错误：缺时戳样本不再向驱动返回失败，不伪造PTS。没有CV710原卡复测，不能保证这就是原卡现场根因。若仍卡住，请保留新日志，capture-video-delivery/time能区分驱动没送视频与样本接收问题。原3090关闭问题也需原机复测。Lecram仍暂停。

本轮通过Windows构建、9个定向目标、115项QtQuick、5070真实NR切换/开启状态关闭/重开、4K37%+NVOF六帧导出、软件/GPU界面原生全屏操作。本机G50视频与48k音频仅短接收，不代表CV710或稳定60fps/长期验收。AMD新NR实卡/HDR/长期未重测，原AMD SR关/录制验证沿用对应提交证据。详见docs整合报告。

仅本地main整合，未push/Release。显示版本2.1.0-int1，不改变正式发布版本。
''','utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for e in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-int1-local-update',base='Complete 2.1.0 NVIDIA or AMD portable package',sourceCommit=head,localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
row=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(row,ensure_ascii=False,indent=1),'utf8');print(json.dumps(row,ensure_ascii=False))
