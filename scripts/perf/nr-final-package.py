"""Local complete NVIDIA candidate using unchanged approved dependencies.

No upload/release or AMD validation. Current build and tracked QML are copied,
test loaders/tools are excluded, a complete manifest and source ZIP are kept.
"""
from pathlib import Path
import importlib.util, json, os, re, shutil, subprocess, sys, zipfile

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3]
assert label.replace('-','').isalnum()
matrix.assert_gpu_tests_idle()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/perf/nr-control.py'),'guard'],check=True)
assert not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain=v1']).strip(), 'Commit the tested source first'
head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip()
parent=BASE/'test-packages'/TASK
package=parent/('Veyra-'+label+'-NVIDIA-win64-portable');assert not package.exists()
source=BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
shutil.copytree(source,package,copy_function=matrix.copy_dependency,
 ignore=shutil.ignore_patterns('*.log','logs','user-data*','*.dmp','*tests.exe','qml-tests','QtTest','Qt6Test.dll','Qt6QuickTest.dll','qoffscreen.dll'))
build=BASE/'build'/TASK/variant
shutil.copy2(build/'veyra_qml_ui.exe',package/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml',package/'qml',dirs_exist_ok=True)
build_shaders=build/'shaders'
if build_shaders.is_dir():shutil.copytree(build_shaders,package/'shaders',dirs_exist_ok=True)
assert not any(p.name.startswith('veyra_') and p.name.endswith('_tests.exe') for p in package.rglob('*.exe'))
assert not any(value in (package/'qml/Veyra/Main.qml').read_text(encoding='utf-8') for value in ('PerfProbe.qml','CloseProbe.qml','ObsProbe.qml','test-loader'))
assert not (package/'runtime_local/amd/nr').exists()
deps={p.relative_to(source).as_posix():matrix.digest(p) for p in source.rglob('*.dll')}
assert all(matrix.digest(package/name)==sha for name,sha in deps.items() if (package/name).is_file())
for path in ROOT.glob('docs/PERF_*.md'):
 dest=package/'docs'/path.name;dest.parent.mkdir(exist_ok=True);shutil.copy2(path,dest)
source_zip=parent/('Veyra-'+label+'-veyra-source.zip')
subprocess.run(['git','-C',str(ROOT),'archive','--format=zip','--output='+str(source_zip),head],check=True)
cache=(build/'CMakeCache.txt').read_text(encoding='utf-8')
display=re.search(r'^VEYRA_DISPLAY_VERSION:STRING=(.+)$',cache,re.M).group(1)
manifest=json.loads((package/'package-manifest.json').read_text(encoding='utf-8-sig'))
manifest.update(candidate=label,displayVersion=display,baseCommit='8cdc612120cbf23ba116a33c3cb0a53e2043f718',
 productCodeCommit='e8f0bd1082293c88a3ca1db851667162e4cef7a9',sourceArchiveCommit=head,worktreeDirty=False,
 releaseReady=False,localOnly=True,gpuPackage='NVIDIA',sourceZipSha256=matrix.digest(source_zip),
 validation='RTX5070/616.56 normal-load candidate; other GPUs, real consoles/capture and physical display latency unverified')
manifest['correspondingSource']['application']='../'+source_zip.name
manifest['correspondingSource']['displayVersionOverride']='-DVEYRA_DISPLAY_VERSION='+display
manifest['files']=[{'path':p.relative_to(package).as_posix(),'size':p.stat().st_size,'sha256':matrix.digest(p)}
 for p in sorted(package.rglob('*')) if p.is_file() and p.name!='package-manifest.json']
(package/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
archive=parent/(package.name+'.zip');assert not archive.exists()
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for path in sorted(package.rglob('*')):
  if path.is_file():z.write(path,arcname=path.relative_to(parent).as_posix())
verify=BASE/'verify'/TASK/label;verify.mkdir(parents=True,exist_ok=False)
with zipfile.ZipFile(archive) as z:z.extractall(verify)
extracted=verify/package.name
for row in manifest['files']:
 assert matrix.digest(extracted/row['path'])==row['sha256'], row['path']
profile=BASE/'tests'/TASK/(label+'-package-profile');profile.mkdir(exist_ok=False)
tmp=BASE/'tmp'/TASK/(label+'-package');tmp.mkdir(exist_ok=False)
logs=BASE/'logs'/TASK/(label+'-package');logs.mkdir(exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/'player.log'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
command=[str(extracted/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','7000',str(matrix.SOURCES['M1'])]
with (logs/'console.log').open('xb') as stream:
 rc=subprocess.run(command,cwd=extracted,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=60).returncode
text=(logs/'player.log').read_text(encoding='utf-8',errors='replace')
assert rc==0 and 'Veyra QML session started' in text and 'playback' in text.lower()
assert not any(value in text for value in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object'))
assert all(matrix.digest(extracted/row['path'])==row['sha256'] for row in manifest['files'])
receipt={'sourceArchiveCommit':head,'productCodeCommit':manifest['productCodeCommit'],'displayVersion':display,
 'package':str(package),'archive':str(archive),'archiveSha256':matrix.digest(archive),'archiveBytes':archive.stat().st_size,
 'sourceZip':str(source_zip),'sourceZipSha256':matrix.digest(source_zip),'verifiedExtraction':str(extracted),
 'fileCount':len(manifest['files']),'command':command,'exitCode':rc,'passed':True,'localOnly':True}
(logs/'result.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('FINAL_LOCAL_PACKAGE',json.dumps(receipt,ensure_ascii=False),flush=True)
