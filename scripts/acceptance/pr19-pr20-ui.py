"""Real QML bridge, own portable app/profile; bounded persistence or export checks."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='pr19-pr20-20261006'
label,case=sys.argv[1:3]
assert case in ('hdr','restore','export')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/pr19-pr20-control.py')],check=True)
out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
app=BASE/'verify'/TASK/'ui-app'
source=BASE/'test-packages/obs-export-amdnr-20261006/Veyra-2.0.4-fix1-NVIDIA-win64-portable'
if not app.exists():shutil.copytree(source,app)
build=BASE/'build'/TASK/'standard'
shutil.copyfile(build/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
shutil.copytree(build/'shaders',app/'shaders',dirs_exist_ok=True)
# The candidate executable loads external QML: exercise the candidate's sources.
shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
profile_name=sys.argv[3] if len(sys.argv)>3 else 'hdr-ui-profile'
assert profile_name.replace('-','').isalnum()
profile=BASE/'tests'/TASK/profile_name if case!='export' else out/'profile'
if case=='restore':assert profile.is_dir()
(out/'outputs').mkdir()
main=app/'qml/Veyra/Main.qml';original=main.read_bytes();text=original.decode('utf8');at=text.rfind('}')
fixture=ROOT/'scripts/acceptance'/('obs-export-amdnr-ui.qml' if case=='export' else 'pr19-pr20-ui.qml')
injection='\nLoader{anchors.fill:parent;source:'+json.dumps(fixture.as_uri())+';onLoaded:{item.caseName='+json.dumps(case)
if case=='export':injection+=';item.media='+json.dumps(str(BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4'))
injection+='}}\n'
main.write_text(text[:at]+injection+text[at:],encoding='utf8')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--quit-after','260000','--export-out',str(out/'outputs'),'--obs-game-capture']
console=logs/(label+'.log');begin=time.monotonic()
try:
    with console.open('xb') as stream:
        try:rc=subprocess.run(args,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=285).returncode
        except subprocess.TimeoutExpired:rc=124
finally:main.write_bytes(original)
body=console.read_text(encoding='utf8',errors='replace')
marker={'hdr':'PR_UI_FIRST_PASS','restore':'PR_UI_RESTORE_PASS','export':'FIELD_EXPORT_PASS'}[case]
errors=[line for line in body.splitlines() if any(s in line for s in ('PR_UI_FAIL','FIELD_UI_FAIL','ReferenceError:','TypeError:','Unable to assign','D3D12 ERROR','D3D12 CORRUPTION'))]
passed=rc==0 and marker in body and not errors
if case!='export':passed=passed and 'PR_UI_SETTINGS_PASS' in body
result=dict(exit=rc,passed=passed,case=case,seconds=time.monotonic()-begin,profile=str(profile),sourceExeSHA=hashlib.sha256((build/'veyra_qml_ui.exe').read_bytes()).hexdigest(),errors=errors)
if case=='export':
    files=list((out/'outputs').glob('*.mp4'));result['exports']=[]
    for file in files:
        probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(file)],timeout=30))
        video=next(s for s in probe['streams'] if s['codec_type']=='video')
        result['exports'].append(dict(path=str(file),video=video))
    passed=passed and len(files)==4 and all(int(v['video']['nb_read_frames'])==60 for v in result['exports']);result['passed']=passed
(logs/(label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False));print('\n'.join(body.splitlines()[-9:]));raise SystemExit(0 if passed else 1)
