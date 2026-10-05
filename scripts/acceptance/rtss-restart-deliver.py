"""Finalize the locally tested NVIDIA repair; never alter the published package."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='rtss-restart-loop-20261004'
OUT=BASE/'test-packages'/TASK; LOGS=BASE/'logs'/TASK; TMP=BASE/'tmp'/TASK
APP=OUT/'Veyra-2.0.3-rtssfix-NVIDIA-win64-portable'
PUBLISHED=BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable'
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result);return result

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/rtss-restart-control.py')],check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit the reviewed local changes first'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
matrix=json.loads((LOGS/'installed-matrix-v3/summary.json').read_text(encoding='utf8'))
stress=json.loads((LOGS/'installed-stress-v4/summary.json').read_text(encoding='utf8'))
assert len(matrix['results'])==11 and len(stress['results'])==3
assert sum(len(r['cases']) for r in stress['results'])==33
assert json.loads((LOGS/'installed-stress-v4/cleanup.json').read_text(encoding='utf8'))['profilesRestored']
assert not json.loads((LOGS/'installed-matrix-v3/cleanup.json').read_text(encoding='utf8'))['remainingOwnedHelpers']
pkg=module('rtss_qml_package',ROOT/'scripts/package-qml-release.py')
vendor=module('rtss_vendor_package',ROOT/'scripts/package-vendor-release.py')
old=json.loads((PUBLISHED/'package-manifest.json').read_text(encoding='utf8'))
product_changes={'veyra_qml_ui.exe','qml/Veyra/Main.qml','qml/Veyra/SettingsPage.qml'}
doc_changes={'docs/'+n for n in ('RTSS_COMPAT_2026-10-03.md','FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md','RELEASE_2.0.3_ACCEPTANCE_2026-10-04.md')}
assert digest(APP/'veyra_qml_ui.exe')==digest(BASE/'build'/TASK/'veyra_qml_ui.exe')
for name in ('Main.qml','SettingsPage.qml'):
    assert (APP/'qml/Veyra'/name).read_bytes()==(ROOT/'qml/Veyra'/name).read_bytes(),'Acceptance loader remains'
for name in doc_changes:
    if (APP/name).exists():shutil.copy2(ROOT/name,APP/name)
shutil.copy2(ROOT/'docs/RTSS_RESTART_LOOP_PLAN_2026-10-04.md',APP/'docs/RTSS_RESTART_LOOP_PLAN_2026-10-04.md')
(APP/'RTSS-修复说明.md').write_text('''# Veyra 2.0.3 RTSS 本地修复版（NVIDIA）

在本目录运行 veyra_qml_ui.exe 即可，组件已齐全。默认监控软件兼容为自动。

修复：孤立 RTSS 加载器或残留共享内存不再被当作 RTSS 屏显服务运行；
点击兼容重启携带本次启动意图，避免重启后再次弹同一提示。
提示区分 RTSS 与 MSI Afterburner，软件绘制背景保持不透明。

RTX 5070 / 616.56、用户实际安装的 RTSS 7.3.5.28314：
11 项启动/重启及 3 套 RTSS 配置 × 11 项真实播放/效果/窗口组合通过。
包括 NR 参数、最高 RTX 4K 超分、DLSS 2X/6X、XeSS 4X、FSR3.1 2X、
VFG 2X Low/4X Medium/8X High、暂停/seek/全屏/窗口恢复。
OSD 2/3 倍、位置、颜色、背景、检测级别、OSD 开关与 60 帧限制实际设置并读回。
这属于短测；VFG 8X High 在 2K30 实时播放会调度降档，不保证 240 fps。

RTSS 测试配置已恢复，测试主进程与加载器已退出。
本包没有带入用户配置、测试脚本、媒体或日志；运行组件字节与正式 NVIDIA 2.0.3 包相同。
本地修复尚未合入 main 或上传 GitHub，原正式包保持原样。
对应 Veyra 源码 ZIP 在本目录的上级；未变的依赖源码沿用正式 2.0.3 资产。
''',encoding='utf8')
changed=[]
for row in old['files']:
    assert (PUBLISHED/row['path']).stat().st_size==row['size'] and digest(PUBLISHED/row['path'])==row['sha256'],('Published payload changed',row['path'])
    if digest(APP/row['path'])!=row['sha256']:changed.append(row['path'])
assert set(changed)<=product_changes|doc_changes,changed
assert product_changes<=set(changed),changed
names=vendor.check_vendor(APP,'NVIDIA')
for name in names:
    pkg.validate_payload(name)
    if name.endswith('.qml'):
        assert 'scripts/acceptance/rtss-restart-' not in (APP/name).read_text(encoding='utf8',errors='ignore')
rows=[dict(path=f.relative_to(APP).as_posix(),size=f.stat().st_size,sha256=digest(f))
      for f in sorted(APP.rglob('*')) if f.is_file() and f.name!='package-manifest.json']
source_name='Veyra-2.0.3-rtssfix-veyra-source.zip'
manifest={k:v for k,v in old.items() if k!='files'}
manifest.update(candidate='2.0.3-rtssfix',displayVersion='2.0.3-rtssfix',baseCommit=commit,sourceArchiveCommit=commit,
                worktreeDirty=False,releaseReady=False,localOnly=True,
                publishedBase='f82f6499ff0db9c36953bcafb752b9be2d7fca4d',
                correspondingSource=dict(application='../'+source_name,dependencies=old['correspondingSource']['dependencies'],
                                         instructions='docs/BUILD_2.0.3.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.3-rtssfix'),files=rows)
write(APP/'package-manifest.json',manifest)
audit=TMP/'package-audit.py'
original=(ROOT/'scripts/acceptance/release-2.0.3-package-audit.py').read_text(encoding='utf8')
assert original.count("TASK='release-2.0.3-20261004'")==1
audit.write_text(original.replace("TASK='release-2.0.3-20261004'","TASK='"+TASK+"'"),encoding='utf8')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP))
subprocess.run([sys.executable,'-B',str(audit),str(APP),'NVIDIA','local-package-audit'],env=env,check=True,timeout=150)
pkg.source_snapshot(OUT,'2.0.3-rtssfix',digest(APP/'veyra_qml_ui.exe'))
source=OUT/source_name
patch=subprocess.check_output(['git','diff','--binary','354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff',commit],cwd=ROOT)
(BASE/'archives'/TASK/'repair-source.patch').write_bytes(patch)
write(OUT/'DELIVERY.json',dict(candidate='2.0.3-rtssfix',app=str(APP/'veyra_qml_ui.exe'),appSHA256=digest(APP/'veyra_qml_ui.exe'),
      sourceCommit=commit,source=str(source),sourceSHA256=digest(source),packageManifestSHA256=digest(APP/'package-manifest.json'),
      payloadFiles=len(rows),changedPublishedPayload=changed,localOnly=True,releaseReady=False,
      startupRestartCases=11,rtssParameterCases=33,logs=str(LOGS),
      limitation='Short RTX5070/616.56 + RTSS7.3.5.28314 test; VFG8 High realtime scheduling reduction observed; no new AMD/OBS/Xbox/long-run validation'))
print('RTSS LOCAL DELIVERY PASS',commit,len(rows),digest(APP/'veyra_qml_ui.exe'),OUT,flush=True)
