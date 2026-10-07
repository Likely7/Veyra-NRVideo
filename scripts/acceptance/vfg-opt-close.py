"""Final source, evidence and local candidate audit; no product mutation."""
from pathlib import Path
import ast,hashlib,json,re,subprocess,sys,zipfile,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007';VERIFY=BASE/'verify'/TASK
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT).decode('utf8').strip()
for script,args in (('vfg-opt-control.py',[]),('vfg-opt-run.py',['audit'])):
 subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance'/script),*args],cwd=ROOT,check=True,timeout=90)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for p in psutil.process_iter(['name']))
subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
assert not git('status','--porcelain=v1'),'Commit completed source/evidence helpers before final audit'
core=['src/pipeline/VfgBackend.cpp','include/veyra/pipeline/VfgBackend.h','src/pipeline/EnhanceGraph.cpp','include/veyra/pipeline/EnhanceGraph.h','src/engine/EngineController.cpp','tests/integration/VfgGpuTests.cpp']
assert not git('diff','--name-only','ddf71b1','--','src','include','shaders','qml','CMakeLists.txt','tests/integration/VfgGpuTests.cpp'),'Product differs from measured build'
changed=git('diff','--name-only','aec800e','HEAD').splitlines()
assert all(Path(p).suffix.lower() in ('.md','.cpp','.h','.py','.qml','.ps1') for p in changed),changed
pythonFiles=list((ROOT/'scripts/acceptance').glob('vfg-opt-*.py'))
for p in pythonFiles:ast.parse(p.read_text(encoding='utf8'),filename=str(p))
measurements=read(VERIFY/'measurements.json');package=read(VERIFY/'candidate-package.json');cold=read(VERIFY/'candidate-cold.json')
assert measurements['buildSource']==package['buildSource'] and measurements['buildExeSha256']==package['exeSha256']==cold['exeSha256'] and cold['passed']
assert sha(BASE/'build'/TASK/'veyra_qml_ui.exe')==package['exeSha256']
app=Path(package['app']);manifest=read(app/'package-manifest.json')
assert manifest['localOnly'] and not manifest['releaseReady'] and manifest['displayVersion']=='2.0.5-vfg-test'
for row in manifest['files']:assert sha(app/row['path'])==row['sha256'],row['path']
assert {x.relative_to(app).as_posix() for x in app.rglob('*') if x.is_file()}=={x['path'] for x in manifest['files']}|{'package-manifest.json'}
assert sha(ROOT/'docs/VFG_OPTIMIZATION_REPORT_2026-10-07.md')==sha(app/'VFG-TEST-NOTES.zh-CN.md')
source=Path(package['source']);assert sha(source)==package['sourceSha256']
with zipfile.ZipFile(source) as z:
 assert z.testzip() is None
 for p in core:
  # git archive is materialized with the repository's Windows text checkout
  # convention here; compare normalized bytes to the exact commit object.
  archived=z.read('Veyra-vfg-test/'+p).replace(bytes([13,10]),bytes([10]))
  committed=subprocess.check_output(['git','show',package['buildSource']+':'+p],cwd=ROOT)
  assert archived==committed,p
 for item in z.infolist():assert not re.search(r'\.(dll|lib|exe|pdb|onnx|f16|hsaco|cubin)$',item.filename,re.I),item.filename
cleanup=read(BASE/'logs'/TASK/'cleanup.json')
result={'passed':True,'head':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'productSource':package['buildSource'],
 'exeSha256':package['exeSha256'],'sourceSha256':package['sourceSha256'],'candidate':str(app),
 'coreSha256':{p:sha(ROOT/p) for p in core},'changedFiles':changed,'pythonSyntaxFiles':len(pythonFiles),
 'mainAndOtherWorktreesPreserved':True,'publishedAppAndRuntimePreserved':True,'noSdkOrRuntimeAddedToGit':True,
 'matched':measurements['matched'],'matrixCases':len(measurements['matrix']),'exportCases':measurements['exportCases'],
 'candidateCold':cold,'removedTemporaryCacheBytes':cleanup['bytes'],'packageManifestSha256':sha(app/'package-manifest.json'),
 'measurementReceiptSha256':sha(VERIFY/'measurements.json'),'sourceArchiveCrcVerified':True}
(VERIFY/'final-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('VFG FINAL AUDIT PASS',result['head'],len(manifest['files']),'candidate payload files',flush=True)
