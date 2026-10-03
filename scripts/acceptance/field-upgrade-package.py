"""Local candidate with audited user assets; never a public release/upload."""
import hashlib, importlib.util, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='field-upgrade-20261003';LABEL='2.0.2-field-20261003'
OUTPUT=BASE/'test-packages'/TASK
STAGE=OUTPUT/('Veyra-'+LABEL+'-win64-portable')
AMD=BASE/'deps/veyra-amd-nr-039-20261003'
LOGS=BASE/'logs'/TASK
QT=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/field-upgrade-control.py')],check=True)
OUTPUT.mkdir(parents=True,exist_ok=True)
command=[sys.executable,'-B',str(ROOT/'scripts/package-qml-release.py'),
    '--build',str(BASE/'build'/TASK),'--runtime-source',str(BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable'),
    '--legacy-licenses',str(BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable/licenses'),
    '--qt',str(QT),'--qt-licenses',str(BASE/'deps/qt-licenses-6.8.3'),
    '--output',str(OUTPUT),'--label',LABEL,'--no-archive']
with (LOGS/'package-stage.log').open('xb') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=290,check=True)
shutil.copytree(AMD,STAGE/'runtime/amd-nr')
for name in ('FIELD_UPGRADE_PLAN_2026-10-03.md','FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md','AMD_NR_INTEGRATION_2026-10-03.md'):
    shutil.copy2(ROOT/'docs'/name,STAGE/'docs'/name)
(STAGE/'LOCAL-FIELD-FIX.md').write_text('''# Veyra 本地修复候选 / Local field-fix candidate

双击 veyra_qml_ui.exe。基于 2.0.2，含本轮 Xbox 输出/重连、导出码率、RTSS 背景和 NVIDIA 插件误判修复。
尚未合并、推送或公开发布；详情见 docs/FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md。

AMD NR 已接入共享图，用户 0.39 模型/HIP 资源已附在 runtime/amd-nr。
仅用于本地实验；权重不是 MIT 源码，不代表公开分发授权。需要 RX9000 和驱动自带 amdhip64_7.dll。
本机 RTX5070 未验证 AMD 推理；当前仅 1080p 像素预算，不支持 HDR/原生 4K AMD NR 导出。
AMD 使用模型强度/残差/保护，style 在 assets/native-game-flags.txt；不加载 ReShade/Magpie 注入模块。
Xbox 真机有声/长稳仍需验收。本机导出测试驱动 616.56，用户 616.92 未直接验证。

Launch veyra_qml_ui.exe. This local 2.0.2 candidate fixes Xbox audio startup and bounded reconnection,
bitrate input, the RTSS software background and NVIDIA module false warnings.
AMD NR is experimental: RX9000 + driver HIP runtime required; 1080p pixel budget, no native 4K NR export or HDR.
AMD inference and real Xbox sound/long-term stability remain unverified. No public release has been made.
''',encoding='utf8')
spec=importlib.util.spec_from_file_location('qml_package',ROOT/'scripts/package-qml-release.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'))
records=[]
for file in sorted(STAGE.rglob('*')):
    if not file.is_file():continue
    name=file.relative_to(STAGE).as_posix()
    if name=='package-manifest.json':continue
    pkg.validate_payload(name)
    records.append({'path':name,'size':file.stat().st_size,'sha256':pkg.digest(file)})
assert not any('lmxxf-test-runtime' in r['path'] or 'fake' in r['path'].lower() or r['path'].endswith('.addon64') for r in records)
manifest.update(files=records,localOnly=True,amdInferenceVerified=False,xboxHardwareVerified=False,
    amdProvenance='runtime/amd-nr/amd-nr-local-manifest.json')
(STAGE/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
(LOGS/'candidate-stage.json').write_text(json.dumps({'stage':str(STAGE),'files':len(records),'source':manifest['baseCommit'],'appSha256':pkg.digest(STAGE/'veyra_qml_ui.exe')},ensure_ascii=False,indent=2),encoding='utf8')
print('LOCAL CANDIDATE STAGED',STAGE,'files',len(records),flush=True)
# Corresponding Veyra source remains physically separate; the pinned external
# MIT runtime source is identified by its origin/build instructions.
source=OUTPUT/('Veyra-'+LABEL+'-source.zip')
subprocess.run(['git','archive','--format=zip','--output',str(source),'HEAD'],cwd=ROOT,timeout=60,check=True)
archive=Path(str(STAGE)+'.zip')
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
    for file in sorted(STAGE.rglob('*')):
        if file.is_file():z.write(file,STAGE.name+'/'+file.relative_to(STAGE).as_posix())
audit=pkg.verify_zip(archive)
archive.with_suffix('.zip.sha256').write_text(audit['sha256']+'  '+archive.name+'\n',encoding='ascii')
(LOGS/'candidate-package-audit.json').write_text(json.dumps(audit,indent=2),encoding='utf8')
print('LOCAL CANDIDATE ARCHIVE PASS',json.dumps(audit),flush=True)
