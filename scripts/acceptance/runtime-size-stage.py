"""Stage a complete owned portable copy with only the audited VFG DLLs."""
import hashlib, importlib.util, json, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
OLD=BASE/'test-packages/vfg-integration-20261003/Veyra-2.0.2-vfg-20261003-win64-portable'
STAGE=BASE/'test-packages'/TASK/'Veyra-2.0.2-slim-20261004-win64-portable'
BUILD=BASE/'build'/TASK
KEEP={'cudart64_12.dll','NVCVImage.dll','nvngxruntime.dll','NVVideoEffects.dll','nvVFXVideoFrameGeneration.dll'}
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()
spec=importlib.util.spec_from_file_location('qml_package',ROOT/'scripts/package-qml-release.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
if sys.argv[1]=='stage':
    STAGE.mkdir(parents=True,exist_ok=False)
    old=json.loads((OLD/'package-manifest.json').read_text(encoding='utf8'))
    omitted=[]
    for row in old['files']:
        rel=Path(row['path']);source=OLD/rel
        assert source.stat().st_size==row['size'] and digest(source)==row['sha256'].lower(),rel
        if rel.as_posix().startswith('runtime/nvidia-vfg/') and rel.name not in KEEP:
            omitted.append(row);continue
        pkg.validate_payload(rel.as_posix())
        dest=STAGE/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,dest)
    shutil.copy2(BUILD/'veyra_qml_ui.exe',STAGE/'veyra_qml_ui.exe')
    for p in BUILD.joinpath('shaders').rglob('*'):
        if p.is_file():
            dst=STAGE/'shaders'/p.relative_to(BUILD/'shaders');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
    vfg=json.loads((STAGE/'vfg-runtime-manifest.json').read_text(encoding='utf8'))
    vfg['files']=[r for r in vfg['files'] if r['name'] in KEEP]
    assert len(vfg['files'])==5
    vfg.update(vfgOnlyDependencySet=True,omittedSdkLibraries=[r['path'].split('/')[-1] for r in omitted],dependencyAudit='docs/RUNTIME_SIZE_ACCEPTANCE_2026-10-04.md')
    (STAGE/'vfg-runtime-manifest.json').write_text(json.dumps(vfg,ensure_ascii=False,indent=2),encoding='utf8')
    tests=BASE/'tests'/TASK/'app';tests.mkdir(parents=True,exist_ok=False)
    for p in STAGE.rglob('*'):
        if not p.is_file():continue
        dst=tests/p.relative_to(STAGE);dst.parent.mkdir(parents=True,exist_ok=True)
        if p.suffix.lower() in {'.exe','.qml','.js','.json','.log'} or p.relative_to(STAGE).parts[0] in {'qml','shaders'}:shutil.copy2(p,dst)
        else:dst.hardlink_to(p)
    for name in ('veyra_vfg_gpu_tests','veyra_vfg_export_probe','veyra_vfg_settings_tests'):shutil.copy2(BUILD/(name+'.exe'),tests/(name+'.exe'))
    fixtures=BASE/'tests'/TASK/'export-basic-v2';fixtures.mkdir(parents=True,exist_ok=True)
    shutil.copy2(BASE/'tests/vfg-integration-20261003/export-basic-v2/input.mp4',fixtures/'input.mp4')
    fixtures=BASE/'tests'/TASK/'ui-v1';fixtures.mkdir(parents=True,exist_ok=True)
    shutil.copy2(BASE/'tests/vfg-integration-20261003/ui-v1/preview-720p30.mp4',fixtures/'preview-720p30.mp4')
    result={'stage':str(STAGE),'testApp':str(tests),'removed':omitted,'removedBytes':sum(r['size'] for r in omitted),'vfgBytes':sum((STAGE/'runtime/nvidia-vfg'/n).stat().st_size for n in KEEP),'appSha256':digest(STAGE/'veyra_qml_ui.exe')}
    (BASE/'logs'/TASK/'stage.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('STAGED',json.dumps({k:v for k,v in result.items() if k!='removed'},ensure_ascii=False),flush=True)
elif sys.argv[1]=='finalize':
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit tested code and reports first'
    for name in ('RUNTIME_SIZE_PLAN_2026-10-04.md','RUNTIME_SIZE_ACCEPTANCE_2026-10-04.md','RUNTIME_SIZE_USER_GUIDE_2026-10-04.md','MINIMAL_EDGE_ACCEPTANCE_2026-10-04.md','HDR_DOVI_PR_REVIEW_2026-10-04.md','WORKLOG.md','CURRENT_STATUS.md'):shutil.copy2(ROOT/'docs'/name,STAGE/'docs'/name)
    shutil.copy2(ROOT/'docs/RUNTIME_SIZE_USER_GUIDE_2026-10-04.md',STAGE/'LOCAL-TEST-PACKAGE.md')
    manifest=json.loads((OLD/'package-manifest.json').read_text(encoding='utf8'))
    records=[]
    for p in sorted(STAGE.rglob('*')):
        if not p.is_file():continue
        rel=p.relative_to(STAGE).as_posix()
        if rel=='package-manifest.json':continue
        pkg.validate_payload(rel)
        assert p.suffix.lower() not in {'.pyd','.pdb','.lib','.whl','.addon64'},rel
        records.append({'path':rel,'size':p.stat().st_size,'sha256':digest(p)})
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
    manifest.update(candidate='2.0.2-slim-20261004',baseCommit=commit,sourceArchiveCommit=commit,worktreeDirty=False,localOnly=True,releaseReady=False,files=records,
                    originalCandidate='2.0.2-vfg-20261003',hdrPrProductChanges=False,slimming='docs/RUNTIME_SIZE_ACCEPTANCE_2026-10-04.md')
    manifest.pop('sourceSnapshot',None)
    manifest['projectSource']={'name':'Veyra-2.0.2-slim-20261004-source.zip','compiledCodeCommit':commit,'documentationMayBeNewer':True,'sdkRuntimeModelsIncluded':False}
    (STAGE/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    print('FINALIZED',len(records),'files',sum(r['size'] for r in records),'bytes',commit,flush=True)
else:raise SystemExit('stage or finalize')
