"""Local NVIDIA candidate from the approved package; independent file copies."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007'
PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
OUT=BASE/'test-packages'/TASK;APP=OUT/'Veyra-2.0.5-VFG-test-NVIDIA-win64-portable'
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
build=json.loads((BASE/'logs'/TASK/'candidate-build-v3.json').read_text(encoding='utf8'));assert build['exit']==0
head=build['sourceHead'];assert head.startswith('ddf71b1')
old=json.loads((PACKAGE/'package-manifest.json').read_text(encoding='utf-8-sig'))
for item in old['files']:assert sha(PACKAGE/item['path'])==item['sha256'].lower(),item['path']
assert not APP.exists();OUT.mkdir(parents=True,exist_ok=True)
shutil.copytree(PACKAGE,APP,copy_function=shutil.copy2)
shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
notes=ROOT/'docs/VFG_OPTIMIZATION_REPORT_2026-10-07.md';assert notes.is_file()
assert '状态：本机VFG专项短测通过' in notes.read_text(encoding='utf8') and '<!--' not in notes.read_text(encoding='utf8')
shutil.copy2(notes,APP/'VFG-TEST-NOTES.zh-CN.md')
source=OUT/'Veyra-2.0.5-VFG-test-source.zip';assert not source.exists()
subprocess.run(['git','archive','--format=zip','--prefix=Veyra-vfg-test/','--output='+str(source),head],cwd=ROOT,check=True,timeout=90)
manifest={'schema':3,'version':'2.0.5','candidate':'2.0.5-vfg-test','displayVersion':'2.0.5-vfg-test','releaseReady':False,'localOnly':True,'gpuPackage':'NVIDIA',
 'baseCommit':head,'sourceArchiveCommit':head,'publishedBaseVersion':'2.0.5','publishedBaseExeSha256':sha(PACKAGE/'veyra_qml_ui.exe'),
 'correspondingSource':{'application':'../'+source.name,'applicationSha256':sha(source),'dependencies':old['correspondingSource']['dependencies'],'dependencySha256':old['correspondingSource']['dependencySha256'],'instructions':'docs/BUILD_2.0.5.md','displayVersionOverride':'-DVEYRA_DISPLAY_VERSION=2.0.5-vfg-test'},
 'knownIssues':['NVIDIA VRAM growth remains unresolved; this is a VFG submission optimization','High quality above 2X and higher Medium multipliers do not all reach the requested output rate on the tested RTX5070'],
 'files':[]}
for file in sorted(APP.rglob('*')):
 if not file.is_file() or file.name=='package-manifest.json':continue
 rel=file.relative_to(APP).as_posix();original=PACKAGE/rel
 if original.is_file():
  assert not os.path.samefile(file,original),'Candidate must not share writable hardlinks'
  if rel!='veyra_qml_ui.exe':assert sha(file)==sha(original),rel
 manifest['files'].append({'path':rel,'size':file.stat().st_size,'sha256':sha(file)})
(APP/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
receipt={'app':str(APP),'exeSha256':sha(APP/'veyra_qml_ui.exe'),'source':str(source),'sourceSha256':sha(source),'files':len(manifest['files']),'runtimeAndQmlUnchanged':True,'independentCopies':True,'buildSource':head}
verify=BASE/'verify'/TASK;verify.mkdir(parents=True,exist_ok=True);(verify/'candidate-package.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
print(json.dumps(receipt,ensure_ascii=False),flush=True)
