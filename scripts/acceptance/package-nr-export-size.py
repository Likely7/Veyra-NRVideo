"""Seal local nre1 EXE/UI update. No runtime, model, user configuration or test media."""
from pathlib import Path
import json,subprocess,hashlib,zipfile,shutil
B=Path('E:/项目/Veyra');T='export-nr-scale-20261025';R=Path(__file__).resolve().parents[2];O=B/'reports'/T;D=B/'build/claude-screen-recording-20261022';P=B/'test-packages'/T/'Veyra-2.1.0-nre1-NVIDIA-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit the verified source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert all(x['exit']==0 for x in json.loads((O/'contracts.json').read_text('utf8')))
for name in ['gpu-exports-all.json','gpu-exports-4k-flow.json']:assert all(x['passed'] for x in json.loads((O/name).read_text('utf8')))
ui=json.loads((O/'ui-preset-export.json').read_text('utf8'));assert ui['passed']
assert all(x['exit']==0 for x in json.loads((O/'image-flow-probes.json').read_text('utf8')))
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(D/'veyra_qml_ui.exe')==ui['exeSha256']
assert not P.exists();P.mkdir(parents=True)
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(R/folder))
for name in ['EXPORT_NR_SCALE_PLAN_2026-10-25.md','EXPORT_NR_SCALE_REPORT_2026-10-25.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md']:copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
note='''# Veyra 2.1.0-nre1 NVIDIA 本地测试更新

需要已有完整2.1.0 NVIDIA便携包。关闭播放器，将本目录文件复制到原包根目录（veyra_qml_ui.exe所在目录），覆盖同名文件，再启动。
本更新仅含EXE、QML、翻译与说明，不含DLL/模型/SDK/日志/用户配置。原package-manifest记录完整底包；更新后程序/UI的身份以LOCAL_UPDATE.json为准。

- 视频导出NR内部处理尺寸遵循每层固定档和手动25%–100%，不再强制原生。50%同时缩放宽高，约为四分之一像素量；最终输出尺寸仍由导出设置决定。
- 例：4K选1080p或50%，NR按1920x1080处理并合成回4K；8K选25%，NR按1920x1080处理并输出8K。
- Auto在视频任务快照中固定最高1440p，AMD保留1080p预算；AMD手动比例先受现有预算限制再缩放。图片仍完整处理。节点视频导出仍维持原有不支持状态，请使用列表预设。
- 导出选择的预设独立于当前预览参数，任务冻结后改滑条不会改变正在导出的任务。降低内部尺寸可能改变NR效果，不能等同原生尺寸画质。
- 包含此前NR手动滑条、fg1后台VRR查询、旋转与录制修复。Lecram暂停。本轮未改补帧调度、NR模型或运行库。
- 本机RTX5070/SF runtime已验证4K/8K合成短片真实导出、每层NR创建尺寸、NVENC编码、输出尺寸/帧数/非黑解码、界面保存37%预设实际导出和图片全尺寸GPU探针。AMD实卡、真实8K黑场素材、HDR、长期画质未在本轮验收；8K短片导出使用零运动输入，另有4K真实NVOF导出用例。

显示版本2.1.0-nre1，PE数值2.1.0.0。隔离分支本地交付，未合main、未push或公开发布。
'''
(P/'README-NRE1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for e in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-nre1-local-update',base='Complete 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(R),localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
