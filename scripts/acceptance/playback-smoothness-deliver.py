"""Create an isolated complete NVIDIA candidate from verified manifest files."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004';LABEL='2.0.3-smoothfix'
OUT=BASE/'test-packages'/TASK;LOGS=BASE/'logs'/TASK;TMP=BASE/'tmp'/TASK
APP=OUT/('Veyra-'+LABEL+'-NVIDIA-win64-portable')
PUBLISHED=BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable'

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/playback-smoothness-control.py')],cwd=ROOT,check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit reviewed source before packaging'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
assert all(r['passed'] for r in json.loads((LOGS/'native-tests.json').read_text(encoding='utf8')))
for test in ('functional-v5-final','layout-fullscreen-v4-final','capability-v1','candidate-software-v2-final','candidate-vfg4-v2-final'):
    assert json.loads((BASE/'tests'/TASK/test/'result.json').read_text(encoding='utf8'))['passed'],test
pkg=module('smooth_qml_package',ROOT/'scripts/package-qml-release.py')
vendor=module('smooth_vendor_package',ROOT/'scripts/package-vendor-release.py')
old=json.loads((PUBLISHED/'package-manifest.json').read_text(encoding='utf8'))
OUT.mkdir(parents=True,exist_ok=False);APP.mkdir()
for row in old['files']:
    src=PUBLISHED/row['path'];dst=APP/row['path']
    assert src.stat().st_size==row['size'] and digest(src)==row['sha256'],('Published identity',row['path'])
    pkg.validate_payload(row['path']);dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(src,dst)
shutil.copyfile(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml/Veyra',APP/'qml/Veyra',dirs_exist_ok=True)
docs=('PLAYBACK_SMOOTHNESS_PLAN_2026-10-04.md','SUBTITLE_ENGINE_2026-09-16.md',
      'RTSS_RESTART_LOOP_PLAN_2026-10-04.md','RTSS_COMPAT_2026-10-03.md')
for name in docs:
    dst=APP/'docs'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'docs'/name,dst)
(APP/'本地修复说明.md').write_text('''# Veyra 2.0.3-smoothfix 本地测试包（NVIDIA）

运行本目录的 veyra_qml_ui.exe，基础依赖与已审计运行组件齐全。

- 自定义文件播放速度 0.25～4.00 倍，支持两位小数，保持原有音调。
- 修复内嵌字幕正文逗号被吃掉、两行/三行字幕缺失及 VTT 的 CRLF。
- 能力查询不再随播放快照反复访问磁盘/重建菜单，保留替换组件发现与灰色入口。
- 专业模式只在顶部保留输入信息，输出分辨率放在旁边；窄窗口进度与按钮分行。
- 修复倍速菜单裁剪/提示覆盖；自定义窗口可接受键盘输入。
- 最大化窗口退出全屏后恢复最大化，普通窗口恢复原尺寸。
- 包含此前本地 RTSS 误报与兼容重启循环修复。

RTX5070 / 616.56 短测：音频18组、字幕30个作者cue、硬件能力56组，
真实Qt播放/自定义倍速/字幕/菜单/720–1600宽度/全屏恢复均通过。
软件UI后台DLSS6X提交约180fps、UI计时采样中位16ms；VFG4X提交中位约115fps。
这些是软件提交/计时指标，不能当作屏幕实际刷新率或端到端延迟。
用户报告的严重后台掉帧尚未复现，未找到根因；显示ETW采集被本机权限拒绝。
新增焦点/吞吐/系统进程节流日志可辅助进一步定位。未新增AMD实卡验收。

此包未合入 main，未上传/替换 GitHub 2.0.3；与原正式包分开保存。
运行库字节、许可证与GPU分包规则保持；不包含AMD NR、SDK、测试媒体、个人配置或日志。
源码ZIP在上级目录，原依赖源码沿用已发布2.0.3资产。完整证据在本机logs任务目录。
''',encoding='utf8')
names=vendor.check_vendor(APP,'NVIDIA')
for name in names:
    pkg.validate_payload(name)
    if name.startswith('qml/Veyra/') and name.endswith('.qml'):
        assert 'scripts/acceptance/' not in (APP/name).read_text(encoding='utf8',errors='ignore'),'Acceptance loader in product'
changed=[r['path'] for r in old['files'] if digest(APP/r['path'])!=r['sha256']]
allowed={'veyra_qml_ui.exe','docs/RTSS_COMPAT_2026-10-03.md'}
allowed.update(p for p in subprocess.check_output(['git','diff','--name-only','f82f6499ff0db9c36953bcafb752b9be2d7fca4d',commit],cwd=ROOT).decode().splitlines() if p.startswith('qml/Veyra/'))
assert set(changed)<=allowed,changed
rows=[dict(path=f.relative_to(APP).as_posix(),size=f.stat().st_size,sha256=digest(f)) for f in sorted(APP.rglob('*')) if f.is_file() and f.name!='package-manifest.json']
source_name='Veyra-'+LABEL+'-veyra-source.zip'
manifest={k:v for k,v in old.items() if k!='files'}
manifest.update(candidate=LABEL,displayVersion=LABEL,baseCommit=commit,sourceArchiveCommit=commit,
    worktreeDirty=False,releaseReady=False,localOnly=True,publishedBase='f82f6499ff0db9c36953bcafb752b9be2d7fca4d',
    correspondingSource=dict(application='../'+source_name,dependencies=old['correspondingSource']['dependencies'],
                             instructions='docs/BUILD_2.0.3.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION='+LABEL),files=rows)
write(APP/'package-manifest.json',manifest)
audit=TMP/'smooth-package-audit.py'
original=(ROOT/'scripts/acceptance/release-2.0.3-package-audit.py').read_text(encoding='utf8')
assert original.count("TASK='release-2.0.3-20261004'")==1
audit.write_text(original.replace("TASK='release-2.0.3-20261004'","TASK='"+TASK+"'"),encoding='utf8')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP))
subprocess.run([sys.executable,'-B',str(audit),str(APP),'NVIDIA','local-package-audit'],env=env,check=True,timeout=150)
source=pkg.source_snapshot(OUT,LABEL,digest(APP/'veyra_qml_ui.exe'))
patch=subprocess.check_output(['git','diff','--binary','354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff',commit],cwd=ROOT)
(BASE/'archives'/TASK/'repair-source.patch').write_bytes(patch)
write(OUT/'DELIVERY.json',dict(candidate=LABEL,app=str(APP/'veyra_qml_ui.exe'),appSHA256=digest(APP/'veyra_qml_ui.exe'),
      sourceCommit=commit,source=str(OUT/source_name),sourceSHA256=source['sha256'],
      packageManifestSHA256=digest(APP/'package-manifest.json'),payloadFiles=len(rows),
      changedPublishedPayload=changed,localOnly=True,releaseReady=False,
      backgroundRootCauseFound=False,logs=str(LOGS),
      limitation='RTX5070/616.56 short tests; severe background drops not reproduced; ETW display events unavailable without privileges; AMD real hardware not tested'))
print('PLAYBACK LOCAL DELIVERY PASS',commit,len(rows),OUT,flush=True)
