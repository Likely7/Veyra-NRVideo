"""Run scoped local regressions with isolated artifacts and no live Xbox login."""
from pathlib import Path
import hashlib, json, os, re, shutil, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = 'capture-xbox-field-20261005'
label = sys.argv[1]; case = sys.argv[2]
assert label.replace('-', '').isalnum()
if case in ('smoke','hot'):
    app=Path(sys.argv[3]).resolve()
    assert app.is_relative_to((BASE/'test-packages'/TASK).resolve()) and (app/'veyra_qml_ui.exe').is_file()
    log=BASE/'logs'/TASK/(label+'.log'); engine_log=log.with_name(label+'-engine.log')
    out=BASE/'tests'/TASK/label; tmp=BASE/'tmp'/TASK/label
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    (out/'screenshots').mkdir()
    guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/capture-xbox-control.py')]
    subprocess.run(guard,check=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
    windows=Path(env['WINDIR'])
    env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),
        VEYRA_LOG_FILE=str(engine_log),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',
        PATH=os.pathsep.join(map(str,(app,windows/'System32',windows,windows/'System32/Wbem'))))
    args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(out/'profile'),'--page','pro',
          '--size','1280x800','--obs-game-capture','--exit-after','190000' if case=='hot' else '8000']
    main=app/'qml/Veyra/Main.qml'; original=main.read_bytes()
    assert original==(ROOT/'qml/Veyra/Main.qml').read_bytes()
    if case=='smoke':
        media=BASE/'tests/field-fixes-20261005/media/fx-embedded.mkv';assert media.is_file()
        args.append(str(media))
    else:
        media=BASE/'tests/hotfix-2.0.0-20261002/media/gta6-1080p30-60s.mp4';assert media.is_file()
        injection=r'''
    Timer {
        interval:250;running:true;repeat:true
        property int stage:0
        property int ticks:0
        property int total:0
        property int current:0
        property int readyTicks:0
        property double beforePosition:0
        property double readyPosition:0
        property var cases:["fsr3","xess","dlss","fsr3"]
        function require(ok,why){if(!ok)throw new Error(why)}
        function next(value){stage=value;ticks=0}
        function startCase(){
            beforePosition=veyra.position
            veyra.fgBackendName=cases[current];veyra.fgMultiplier=2;veyra.fgEnabled=true
            readyTicks=0
            next(2)
        }
        onTriggered:{try{
            ++ticks;++total
            require(total<720,"180s bounded deadline stage="+stage)
            require(!veyra.failed,"playback failed "+veyra.statusText)
            if(stage===0){
                if(ticks<8)return
                veyra.muted=true;veyra.nodeMode=0;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.openPath(__MEDIA__);next(1)
            }else if(stage===1){
                if(!veyra.running||veyra.applying||veyra.position<.5)return
                startCase()
            }else if(stage===2){
                if(veyra.applying||!veyra.fgActive){readyTicks=0;return}
                if(!readyTicks){readyPosition=veyra.position;readyTicks=1;return}
                if(++readyTicks<16)return
                require(veyra.running&&veyra.fgActive,"FG not active "+cases[current])
                require(veyra.fgBackendName===cases[current]&&veyra.fgMultiplier===2,"backend not applied")
                require(veyra.position>readyPosition+1,"source stopped after ready "+cases[current])
                console.log("STREAM_FIX_FG_CASE",cases[current],veyra.position,veyra.submitFps,veyra.fgProviderText)
                ++current
                if(current<cases.length){startCase();return}
                beforePosition=veyra.position;veyra.fgEnabled=false;next(3)
            }else if(stage===3){
                if(ticks<12||veyra.applying)return
                require(veyra.running&&!veyra.fgActive&&veyra.position>beforePosition+.5,"FG disable stopped playback")
                veyra.nrEnabled=true
                const first=veyra.nrLayers[0].index
                require(veyra.setNrLayerParameter(first,"runtime",3),"original NR selection")
                require(veyra.setNrLayerParameter(first,"style",1),"NR style 1")
                require(veyra.setNrLayerParameter(first,"total",5),"NR strength 5")
                require(veyra.setNrLayerParameter(first,"correctionEnabled",1),"NR protection")
                require(veyra.duplicateNrLayer(first)>=0,"second NR layer")
                const second=veyra.nrLayers[1].index
                require(veyra.setNrLayerParameter(second,"style",2),"second NR style 2")
                require(veyra.setNrLayerParameter(second,"correctionAuto",0),"second manual NR")
                beforePosition=veyra.position;readyTicks=0;next(4)
            }else if(stage===4){
                if(veyra.applying||!veyra.nrActive){readyTicks=0;return}
                if(!readyTicks){readyPosition=veyra.position;readyTicks=1;return}
                if(++readyTicks<16)return
                require(veyra.running&&veyra.nrActive&&veyra.nrLayers.length===2,"two-layer NR inactive")
                require(veyra.position>readyPosition+.5,"NR preview stopped after ready")
                require(veyra.setPreference("screenshotDir",__SCREENSHOTS__),"screenshot directory")
                veyra.takeScreenshot()
                console.log("STREAM_FIX_NR_CASE",JSON.stringify(veyra.nrLayers));next(5)
            }else if(stage===5){
                if(ticks<8)return
                console.log("STREAM_FIX_HOT_PASS");Qt.quit()
            }
        }catch(e){console.error("STREAM_FIX_HOT_FAIL",e.message);Qt.quit()}}
    }
'''.replace('__MEDIA__',json.dumps(str(media))).replace('__SCREENSHOTS__',json.dumps(str(out/'screenshots')))
        source=original.decode('utf8'); at=source.rfind('}');assert at>0
        main.write_text(source[:at]+injection+source[at:],encoding='utf8')
    begin=time.monotonic()
    try:
        with log.open('xb') as console:
            try:rc=subprocess.run(args,cwd=app,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=200).returncode
            except subprocess.TimeoutExpired:rc=124
    finally:main.write_bytes(original)
    console=log.read_text(encoding='utf8',errors='replace')
    text=engine_log.read_text(encoding='utf8',errors='replace') if engine_log.exists() else ''
    bad=[line for line in (text+'\n'+console).splitlines() if any(word in line for word in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object','STREAM_FIX_HOT_FAIL'))]
    ass=[line for line in text.splitlines() if '[libass]' in line]
    passed=rc==0 and not bad
    if case=='smoke':passed=passed and any('track ready events=5' in line for line in ass) and any('fonts configured container=1' in line for line in ass)
    else:
        passed=passed and 'STREAM_FIX_HOT_PASS' in console and console.count('STREAM_FIX_FG_CASE')==4 and 'STREAM_FIX_NR_CASE' in console and bool(list((out/'screenshots').glob('*.png')))
    assert main.read_bytes()==original
    result={'passed':passed,'exit':rc,'seconds':round(time.monotonic()-begin,3),'args':args,
        'exeSha256':hashlib.sha256((app/'veyra_qml_ui.exe').read_bytes()).hexdigest(),
        'mainQmlSha256':hashlib.sha256(original).hexdigest(),'log':str(log),'engineLog':str(engine_log),
        'libass':ass[:8],'errors':bad[:20]}
    log.with_suffix('.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('LOCAL APP',case,'PASS' if passed else 'FAIL',log)
    print('\n'.join(bad[:12] or [line for line in console.splitlines() if 'STREAM_FIX_' in line]))
    subprocess.run(guard,check=True)
    raise SystemExit(0 if passed else 1)
BUILD = BASE/'build'/TASK
APP = BASE/'test-packages/nr-strength-protection-20261005/Veyra-2.0.3-nr-controls2-NVIDIA-win64-portable'
cases = {'xbox': 'veyra_xbox_tests', 'scene': 'veyra_scene_tests',
         'timing': 'veyra_live_timing_tests', 'codec': 'veyra_capture_compressed_tests'}
assert case in cases
LOG = BASE/'logs'/TASK/(label+'.log'); TMP = BASE/'tmp'/TASK/label
OUT = BASE/'tests'/TASK/label
for p in (LOG.parent, TMP, OUT): p.mkdir(parents=True, exist_ok=True)
guard = [sys.executable, '-B', str(ROOT/'scripts/acceptance/capture-xbox-control.py')]
subprocess.run(guard, check=True)
exe = BUILD/(cases[case]+'.exe'); assert exe.is_file()
args = [str(exe)]
fixture = None
if case == 'codec':
    fixture = Path(sys.argv[3]); assert fixture.resolve().is_relative_to(BASE.resolve()) and fixture.is_file()
    # The inherited main(char**) fixture loader is ANSI on Windows. Run from
    # the isolated Unicode directory with an ASCII relative media argument.
    local_fixture = OUT/('input'+fixture.suffix)
    assert not local_fixture.exists()
    shutil.copy2(fixture, local_fixture)
    assert hashlib.sha256(local_fixture.read_bytes()).digest() == hashlib.sha256(fixture.read_bytes()).digest()
    args.append(local_fixture.name)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith('VEYRA_')}
env.update(TEMP=str(TMP), TMP=str(TMP), VEYRA_LOG_FILE=str(OUT/'product.log'),
           PATH=str(APP)+os.pathsep+env.get('PATH', ''))
begin = time.monotonic()
with LOG.open('xb') as log:
    try:
        rc = subprocess.run(args, cwd=OUT, env=env, stdout=log,
                            stderr=subprocess.STDOUT, timeout=290).returncode
    except subprocess.TimeoutExpired:
        rc = 124
content = LOG.read_text(encoding='utf8', errors='replace')
passed = rc == 0 and not re.search(r'^(?:FAIL\b|FAILURES\b|SKIP\b)', content, re.MULTILINE)
result = {'passed':passed, 'exit':rc, 'seconds':round(time.monotonic()-begin, 3),
          'args':args, 'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(), 'log':str(LOG)}
if fixture: result['fixtureSha256'] = hashlib.sha256(fixture.read_bytes()).hexdigest()
LOG.with_suffix('.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
lines=[line for line in content.splitlines() if re.match(r'^(?:\[?(?:PASS|FAIL)\b|ALL PASS\b|FAILURES\b|SKIP\b|xbox tests:|SCENE-TESTS:|hardware frames=|software frames=)',line)]
print('\n'.join(lines[-90:])); print('LOCAL TEST', case, 'PASS' if passed else 'FAIL', LOG)
subprocess.run(guard, check=True)
raise SystemExit(0 if passed else 1)
