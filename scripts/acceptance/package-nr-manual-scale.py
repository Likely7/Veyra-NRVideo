"""Seal a small local update: application/UI only, no runtime or user data."""
from pathlib import Path
import json,hashlib,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='nr-manual-scale-20261024';ROOT=Path(__file__).resolve().parents[2]
OUT=B/'test-packages'/T;REPORT=B/'reports'/T;BUILD=B/'build/claude-screen-recording-20261022'
head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain']).strip(),'commit the source before sealing'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
pkg=OUT/'Veyra-2.1.0-nr1-NVIDIA-update';assert not pkg.exists();pkg.mkdir(parents=True)
def copy(src,relative):
    dst=pkg/relative;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(BUILD/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (ROOT/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(ROOT/folder))
for name in ['NR_MANUAL_SCALE_PLAN_2026-10-24.md','NR_MANUAL_SCALE_REPORT_2026-10-24.md','FG_SCHEDULING_LOG_DIAGNOSIS_2026-10-24.md']:
    copy(ROOT/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(ROOT/name,name)
note='''# Veyra 2.1.0-nr1 本地小更新（NVIDIA）

这是更新层，需要已有完整的2.1.0 NVIDIA便携包。
关闭播放器，把本目录里的文件复制到原包根目录（veyra_qml_ui.exe所在目录），覆盖同名文件，再启动。
本更新不包含、不替换任何DLL/模型/运行库，不包含用户配置。不要将文件放进额外嵌套目录后直接启动EXE。

- NR内部处理分辨率新增“手动”：25%–100%，1%步进，松开滑条后生效；支持键盘、输入与重置。
- 比例针对该层输入的宽高；50%约为四分之一像素，输出尺寸不变。仅视频预览，图片/视频导出合同不变。
- 列表/节点共用，每层独立，当前手动比例保存在预设/会话中；旧版不识别新手动档，应使用本更新读取。
- 包含当前分支此前的录制、AMD超分关闭和画面旋转修复。Lecram仍暂停。
- 补帧“调度降档”本轮只排查，未改调度代码；详细结论见docs/FG_SCHEDULING_LOG_DIAGNOSIS_2026-10-24.md。
- 本机RTX5070通过1080p/4K真实NR推理、节点、重启恢复与110项QtQuick测试；AMD实卡/HDR/真实采集长期运行未在本轮验收。

内部版本2.1.0-nr1，PE数值版本仍为2.1.0.0。LOCAL_UPDATE.json记录更新层与源码提交；原package-manifest.json只代表原完整包，更新后的EXE/UI以LOCAL_UPDATE.json为准。
源码在本机Veyra工作树；这是本地测试更新，未合main、未推送或公开发布。GPL及第三方声明随更新保留。
'''
(pkg/'README-NR1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(pkg).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(pkg.rglob('*')) if p.is_file()]
assert not any(Path(x['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil'] for x in files)
manifest=dict(version='2.1.0-nr1-local-update',base='Complete Veyra 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(ROOT),localOnly=True,releaseReady=False,containsRuntime=False,files=files)
(pkg/'LOCAL_UPDATE.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=1),'utf8')
archive=OUT/(pkg.name+'.zip');assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in pkg.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(pkg).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,package=str(pkg),zip=str(archive),bytes=archive.stat().st_size,sha256=sha(archive),exeSha256=sha(pkg/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(REPORT/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
