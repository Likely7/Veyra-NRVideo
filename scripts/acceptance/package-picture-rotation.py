"""Local rotation packages. Never overwrites an existing package or donor link.

usage: python -X utf8 -B -I scripts/acceptance/package-picture-rotation.py
"""
from pathlib import Path
import hashlib,json,os,subprocess,zipfile

B=Path('E:/项目/Veyra'); T='codex-picture-rotation-20261023'
ROOT=Path(__file__).resolve().parents[2];BUILD=B/'build/claude-screen-recording-20261022'
OUT=B/'test-packages'/T;REPORT=B/'reports'/T
for p in [OUT,REPORT]:p.mkdir(parents=True,exist_ok=True)
head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip(),'commit source before sealing'

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def replace(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():p.unlink()
    p.write_bytes(data)

note='''# Veyra 2.1.0 画面旋转本地测试版（rot1）

完整解压，运行 veyra_qml_ui.exe。构建沿用内部 2.1.0-rec 标识，功能以本文件和清单 rot1 为准。

- 极简播放条、专业播放条和全屏播放条均有顺时针旋转小按钮，每次90°，四次恢复。
- 极简窗口按横竖比例调整，专业/全屏画面适配预览区，控件保持正向。
- 已有视频方向信息与手动旋转叠加。旋转时重置缩放/平移，取消未完成的保护选区。
- 切换模式、暂停、seek、进出全屏保留角度；新开片源清零。
- 本功能改变播放视图；截图、录制与导出仍使用处理纹理的原方向，不改写媒体。
- 游戏模式旋转时沿用非原位预览策略：保留键盘/手柄，拦截错位鼠标穿透。
- 包含上一轮 AMD 超分关闭与录制修复，专有运行库/模型/已编译着色器保持原样。

本地验证范围和未验项见 docs/PICTURE_ROTATION_REPORT_2026-10-23.md。不宣称绝无bug。
尚未合入main或公开发布，Lecram按用户要求暂停。
'''
results=[]
for vendor in ['NVIDIA','AMD']:
    donor=B/'releases/release-2.1.0-20261019/runtime-base'/vendor
    pkg=OUT/f'Veyra-2.1.0-rot1-{vendor}-win64-portable'
    assert donor.is_dir() and not pkg.exists(),pkg
    for src in donor.rglob('*'):
        dst=pkg/src.relative_to(donor)
        if src.is_dir():dst.mkdir(parents=True,exist_ok=True)
        else:os.link(src,dst)
    replace(pkg/'veyra_qml_ui.exe',(BUILD/'veyra_qml_ui.exe').read_bytes())
    for folder in ['qml','i18n']:
        for src in (ROOT/folder).rglob('*'):
            if src.is_file():replace(pkg/folder/src.relative_to(ROOT/folder),src.read_bytes())
    for name in ['CLAUDE_HANDOFF_PLAN_2026-10-22.md','CLAUDE_HANDOFF_REPORT_2026-10-22.md','PICTURE_ROTATION_PLAN_2026-10-23.md','PICTURE_ROTATION_REPORT_2026-10-23.md']:
        replace(pkg/'docs'/name,(ROOT/'docs'/name).read_bytes())
    replace(pkg/'README-TEST.zh-CN.md',note.encode('utf8'))
    old=json.loads((donor/'package-manifest.json').read_text('utf8'));unchanged=[]
    for e in old['files']:
        if e['path'].lower().endswith(('.dll','.onnx','.tflite','.dxil','.hsaco')):
            assert sha(pkg/e['path'])==e['sha256'].lower()==sha(donor/e['path']),e['path']
            unchanged.append(e['path'])
    files=[dict(path=p.relative_to(pkg).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in sorted(pkg.rglob('*')) if p.is_file() and p.name!='package-manifest.json']
    manifest=dict(old);manifest.update(candidate='2.1.0-rot1-local',sourceArchiveCommit=head,productSourceCommit=head,sourceParent='4eee190617615bbd55fea2fc84d171ec14639df1',validationRecord='docs/PICTURE_ROTATION_REPORT_2026-10-23.md',exeSha256=sha(pkg/'veyra_qml_ui.exe'),files=files,localOnly=True,releaseReady=False)
    replace(pkg/'package-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2).encode('utf8'))
    archive=Path(str(pkg)+'.zip');assert not archive.exists(),archive
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
        for p in pkg.rglob('*'):
            if p.is_file():z.write(p,pkg.name+'/'+p.relative_to(pkg).as_posix())
    with zipfile.ZipFile(archive) as z:assert z.testzip() is None
    result=dict(vendor=vendor,package=str(pkg),zip=str(archive),source=head,exeSha256=manifest['exeSha256'],zipSha256=sha(archive),zipBytes=archive.stat().st_size,manifestFiles=len(files),unchangedRuntimeModelShaderFiles=len(unchanged))
    results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
(REPORT/'packages.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
