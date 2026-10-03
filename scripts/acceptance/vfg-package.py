"""Local VFG + field-fix candidate; official runtime separate from Veyra source."""
import hashlib, importlib.util, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-integration-20261003';LABEL='2.0.2-vfg-20261003'
OUTPUT=BASE/'test-packages'/TASK;STAGE=OUTPUT/('Veyra-'+LABEL+'-win64-portable');LOGS=BASE/'logs'/TASK
SDK=BASE/'deps/vfg-python-20261003';WHEEL=BASE/'downloads/vfg-research-20261003/nvidia_vfx-0.2.0.0-cp311-cp311-win_amd64.whl'
NAMES=('cudart64_12.dll','nppc64_12.dll','nppial64_12.dll','nppicc64_12.dll','nppidei64_12.dll','nppif64_12.dll','nppig64_12.dll','nppim64_12.dll','nppist64_12.dll','nppitc64_12.dll','NVCVImage.dll','nvngxruntime.dll','NVVideoEffects.dll','nvVFXVideoFrameGeneration.dll')
spec=importlib.util.spec_from_file_location('qml_package',ROOT/'scripts/package-qml-release.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-control.py')],check=True)
mode=sys.argv[1]
if mode=='stage':
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit the validated source before staging'
    assert pkg.digest(WHEEL)=='5aaf6a42bc6b6dbbf52fcb714194c994a6893cbbf7ada38bc2165a1f83e4a6fc'
    OUTPUT.mkdir(parents=True,exist_ok=True)
    command=[sys.executable,'-B',str(ROOT/'scripts/package-qml-release.py'),'--build',str(BASE/'build'/TASK),'--runtime-source',str(BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable'),'--legacy-licenses',str(BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable/licenses'),'--qt','C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64','--qt-licenses',str(BASE/'deps/qt-licenses-6.8.3'),'--output',str(OUTPUT),'--label',LABEL,'--no-archive']
    with (LOGS/'package-stage.log').open('xb') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=290,check=True)
    shutil.copytree(BASE/'deps/veyra-amd-nr-039-20261003',STAGE/'runtime/amd-nr')
    target=STAGE/'runtime/nvidia/vfg';target.mkdir(parents=True)
    records=[]
    for name in NAMES:
        source=SDK/'nvvfx/libs'/name;shutil.copy2(source,target/name)
        records.append({'path':'runtime/nvidia/vfg/'+name,'name':name,'source':str(source),'size':source.stat().st_size,'sha256':pkg.digest(source),'experimental':True,'removable':True,'modified':False})
    identityFile=BASE/'tmp'/TASK/'vfg-identities.json';identityFile.write_text(json.dumps(records,ensure_ascii=False),encoding='utf8')
    env=os.environ.copy();env.pop('PSModulePath',None);env['VEYRA_VFG_IDENTITIES']=str(identityFile)
    script=r'''$ErrorActionPreference='Stop'; @(Get-Content -LiteralPath $env:VEYRA_VFG_IDENTITIES -Encoding UTF8 -Raw | ConvertFrom-Json) | ForEach-Object { $s=Get-AuthenticodeSignature -LiteralPath $_.source; $v=[System.Diagnostics.FileVersionInfo]::GetVersionInfo($_.source); [PSCustomObject]@{name=$_.name;version=$v.FileVersion;signature=[string]$s.Status;signer=$s.SignerCertificate.Subject} } | ConvertTo-Json -Depth 4'''
    identities=json.loads(subprocess.check_output(['powershell','-NoProfile','-NonInteractive','-Command',script],env=env,timeout=90).decode('utf8-sig'))
    for r in records:
        identity=next(x for x in identities if x['name']==r['name']);assert identity['signature']=='Valid' and 'NVIDIA' in identity['signer'],identity
        r.update(identity);r.pop('source')
    vfg={'schema':1,'localOnly':True,'runtime':'NVIDIA Video Effects SDK 1.3.0 / nvidia-vfx 0.2.0.0','sourceWheel':WHEEL.name,'sourceWheelBytes':WHEEL.stat().st_size,'sourceWheelSha256':pkg.digest(WHEEL),'license':'licenses/nvidia-vfg','publicDistributionAudited':False,'pythonRequired':False,'files':records}
    (STAGE/'vfg-runtime-manifest.json').write_text(json.dumps(vfg,ensure_ascii=False,indent=2),encoding='utf8')
    shutil.copytree(SDK/'nvidia_vfx-0.2.0.0.dist-info/licenses/packaging',STAGE/'licenses/nvidia-vfg')
    samples=STAGE/'licenses/vfg-samples';samples.mkdir();shutil.copy2(BASE/'deps/vfg-samples-20261003/LICENSE',samples/'MIT.txt')
    (STAGE/'LOCAL-VFG.md').write_text('''# Veyra VFG 本地候选

双击 veyra_qml_ui.exe。专业页添加补帧，选择 NVIDIA VFG，再选倍率和质量；节点模式同样可用。
包含 2X / 3X / 4X / 5X / 6X / 7X / 8X 和低 / 中 / 高三档，默认中档。
预览、视频导出、预设与重启保存均接入原生 VFG。运行组件已放在 runtime/nvidia/vfg，无需 Python。
RTX 40 / 50 是官方 Windows 支持范围；本机仅 RTX 5070、616.56 已测试。高倍率为实验选项。
高档 8X 运算成本较高，实时播放可能降档或跳过预览帧；离线导出完整处理，不丢源帧。
软件显示的是提交帧率。它不等于屏幕实际显示帧率，也不证明端到端延迟降低。
本包还保留 Xbox 音频/重连、码率编辑、RTSS 背景、NVIDIA App 误检测修复和 AMD NR 0.39。
Xbox 真机有声/长期稳定、AMD RX9000 推理、RTX40、616.92、HDR色度专项仍未实机验收。
这是本地实验候选，未合并 main、未推送、未公开发布。第三方组件按独立 manifest 与许可记录。
详见 docs/VFG_INTEGRATION_ACCEPTANCE_2026-10-03.md 和 docs/FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md。
''',encoding='utf8')
    for name in ('VFG_INTEGRATION_EXECUTION_2026-10-03.md','VFG_INTEGRATION_ACCEPTANCE_2026-10-03.md','VFG_RESEARCH_AND_INTEGRATION_PLAN_2026-10-03.md','FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md','AMD_NR_INTEGRATION_2026-10-03.md'):shutil.copy2(ROOT/'docs'/name,STAGE/'docs'/name)
    print('LOCAL VFG CANDIDATE STAGED',STAGE,flush=True)
elif mode=='archive':
    assert STAGE.exists()
    # Restore source documents before hashing; a smoke test restores its QML loader.
    for name in ('VFG_INTEGRATION_EXECUTION_2026-10-03.md','VFG_INTEGRATION_ACCEPTANCE_2026-10-03.md'):shutil.copy2(ROOT/'docs'/name,STAGE/'docs'/name)
    manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'));records=[]
    for file in sorted(STAGE.rglob('*')):
        if not file.is_file():continue
        name=file.relative_to(STAGE).as_posix()
        if name=='package-manifest.json':continue
        pkg.validate_payload(name)
        records.append({'path':name,'size':file.stat().st_size,'sha256':pkg.digest(file)})
    assert not any('fake' in r['path'].lower() or r['path'].endswith(('.pyd','.addon64','.pdb','.lib','.whl')) for r in records)
    manifest.update(files=records,localOnly=True,vfgProvenance='vfg-runtime-manifest.json',amdProvenance='runtime/amd-nr/amd-nr-local-manifest.json',rtx40HardwareVerified=False,amdInferenceVerified=False,xboxHardwareVerified=False)
    (STAGE/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    source=OUTPUT/('Veyra-'+LABEL+'-source.zip')
    # The payload's source commit is the implementation checkpoint; later docs
    # commits contain acceptance evidence but cannot silently repin its binaries.
    subprocess.run(['git','archive','--format=zip','--output',str(source),manifest['baseCommit']],cwd=ROOT,timeout=60,check=True)
    source.with_suffix('.zip.sha256').write_text(pkg.digest(source)+'  '+source.name+'\n',encoding='ascii')
    archive=Path(str(STAGE)+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
        for file in sorted(STAGE.rglob('*')):
            if file.is_file():z.write(file,STAGE.name+'/'+file.relative_to(STAGE).as_posix())
    audit=pkg.verify_zip(archive);audit.update(bytes=archive.stat().st_size,sourceCommit=manifest['baseCommit'],appSha256=pkg.digest(STAGE/'veyra_qml_ui.exe'),sourceZip=str(source),sourceSha256=pkg.digest(source))
    archive.with_suffix('.zip.sha256').write_text(audit['sha256']+'  '+archive.name+'\n',encoding='ascii')
    (LOGS/'candidate-package-audit.json').write_text(json.dumps(audit,indent=2),encoding='utf8')
    print('LOCAL VFG CANDIDATE ARCHIVE PASS',json.dumps(audit),flush=True)
else:raise SystemExit('stage or archive')
