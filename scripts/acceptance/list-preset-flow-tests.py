"""2.0.4 regression using the existing field-test fixtures; no live Xbox login."""
from pathlib import Path
import hashlib, json, os, re, shutil, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = 'list-preset-flow-20261006'
label = sys.argv[1]; case = sys.argv[2]
assert label.replace('-', '').isalnum()
if case == 'qml':
    app=BASE/'test-packages'/TASK/'Veyra-2.0.4-NVIDIA-win64-portable'
    qt=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
    out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;log=BASE/'logs'/TASK/(label+'.log')
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    for p in app.glob('Qt6*.dll'):shutil.copy2(p,out/p.name)
    for name in ('Qt6Test.dll','Qt6QuickTest.dll'):shutil.copy2(qt/'bin'/name,out/name)
    shutil.copy2(BASE/'build'/TASK/'veyra_qml_quick_tests.exe',out/'veyra_qml_quick_tests.exe')
    shutil.copytree(app/'qml',out/'qml');shutil.copytree(qt/'qml/QtTest',out/'qml/QtTest')
    shutil.copytree(ROOT/'tests/qml/quick',out/'qml-tests')
    env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',
        QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QT_QUICK_CONTROLS_STYLE='Basic',
        QT_QPA_PLATFORM_PLUGIN_PATH=str(qt/'plugins/platforms'),VEYRA_EXPORT_TEST_ARTIFACTS=str(out/'artifacts'),PATH=str(out)+os.pathsep+str(qt/'bin')+os.pathsep+env.get('PATH',''))
    (out/'artifacts').mkdir()
    args=[str(out/'veyra_qml_quick_tests.exe'),'-input',str(out/'qml-tests')]
    with log.open('xb') as console:
        try:rc=subprocess.run(args,cwd=out,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=290).returncode
        except subprocess.TimeoutExpired:rc=124
    content=log.read_text(encoding='utf8',errors='replace')
    passed=rc==0 and '0 failed' in content and 'SliderNumberInput::test_' in content
    log.with_suffix('.json').write_text(json.dumps({'passed':passed,'exit':rc,'args':args,'log':str(log)},ensure_ascii=False,indent=2),encoding='utf8')
    print(content[-5000:]);raise SystemExit(0 if passed else 1)
if case in ('smoke','smoke-native','hot','ui','ui-restore','nr-export','nr-restore'):
    is_smoke=case.startswith('smoke')
    app=Path(sys.argv[3]).resolve()
    assert app.is_relative_to((BASE/'test-packages'/TASK).resolve()) and (app/'veyra_qml_ui.exe').is_file()
    log=BASE/'logs'/TASK/(label+'.log'); engine_log=log.with_name(label+'-engine.log')
    out=BASE/'tests'/TASK/label; tmp=BASE/'tmp'/TASK/label
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    (out/'screenshots').mkdir()
    guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/list-preset-flow-control.py')]
    subprocess.run(guard,check=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
    windows=Path(env['WINDIR'])
    env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),
        VEYRA_LOG_FILE=str(engine_log),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',
        PATH=os.pathsep.join(map(str,(app,windows/'System32',windows,windows/'System32/Wbem'))))
    args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(out/'profile'),'--page','pro',
          '--size','1280x900','--obs-game-capture','--exit-after','250000' if case.startswith('nr-') else '8000' if is_smoke else '190000']
    if case=='smoke-native':args.remove('--obs-game-capture')
    main=app/'qml/Veyra/Main.qml'; original=main.read_bytes()
    assert original==(ROOT/'qml/Veyra/Main.qml').read_bytes()
    if is_smoke:
        media=BASE/'tests/field-fixes-20261005/media/fx-embedded.mkv';assert media.is_file()
        args.append(str(media))
    elif case in ('ui','ui-restore','nr-export','nr-restore'):
        is_nr=case.startswith('nr-');restore=case.endswith('restore')
        media=BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4' if is_nr else BASE/'tests/hotfix-2.0.0-20261002/media/gta6-1080p30-60s.mp4'
        assert media.is_file()
        profile_receipt=BASE/'logs'/TASK/('nr-profile.json' if is_nr else 'ui-profile.json')
        if restore:
            profile=Path(json.loads(profile_receipt.read_text(encoding='utf8'))['profile'])
            args[args.index('--data-dir')+1]=str(profile)
            if not is_nr:
                prefs=profile/'qml-preferences.v1.json'
                values=json.loads(prefs.read_text(encoding='utf8'))
                values['contentRate']=2 # Must not override the selected default preset on restart.
                values['fgPresets']=[{'name':'legacy inactive FG entry','flowQuality':99}]
                prefs.write_text(json.dumps(values,ensure_ascii=False,indent=2),encoding='utf8')
        else:profile_receipt.write_text(json.dumps({'profile':str(out/'profile')}),encoding='utf8')
        if is_nr:
            (out/'outputs').mkdir()
            args += ['--export-out',str(out/'outputs')]
            source_file=ROOT/'scripts/acceptance/nr-protection-ui.qml'
            fixture=source_file.read_text(encoding='utf8')
            save_call='veyra.savePresetAs("NR correction manual five",1,false)'
            assert fixture.count(save_call)==1
            fixture=fixture.replace(save_call,'veyra.savePresetAs("NR correction manual five",17,false)')
            anchor='                test.require(veyra.savePresetAs("NR correction manual five",17,false)'
            assert fixture.count(anchor)==1
            fixture=fixture.replace(anchor,'                veyra.flowQuality=0;veyra.amdFlowHalf=true;veyra.contentRate=3\n'+anchor)
            fixture=fixture.replace('                veyra.startExport();',
                '                test.require(veyra.selectExportPreset(test.savedIndex),"shared-flow export preset selection");\n'+
                '                veyra.flowQuality=2;veyra.amdFlowHalf=false;veyra.contentRate=0;\n'+
                '                console.log("LIST_PRESET_FLOW_EXPORT_SELECTED",veyra.exportPresetName);\n'+
                '                veyra.startExport();')
            source_file=out/'nr-shared-flow-export.qml';source_file.write_text(fixture,encoding='utf8')

            properties='item.media='+json.dumps(str(media))+';item.evidence='+json.dumps(str(out))+';item.resume='+str(restore).lower()+';item.multi=false;item.visual=false;item.styleFollowup=true'
        else:
            source_file=ROOT/'scripts/acceptance/list-preset-flow-ui.qml'
            properties='item.media='+json.dumps(str(media))+';item.restore='+str(restore).lower()+';item.evidence='+json.dumps(str(out))+';item.dialogHost=dialogs'
        injection='\nLoader { anchors.fill: parent; source: '+json.dumps(source_file.as_uri())+'; onLoaded: { '+properties+' } }\n'
        source=original.decode('utf8');at=source.rfind('}');assert at>0
        main.write_text(source[:at]+injection+source[at:],encoding='utf8')
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
            try:rc=subprocess.run(args,cwd=app,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=270).returncode
            except subprocess.TimeoutExpired:rc=124
    finally:main.write_bytes(original)
    console=log.read_text(encoding='utf8',errors='replace')
    text=engine_log.read_text(encoding='utf8',errors='replace') if engine_log.exists() else ''
    bad=[line for line in (text+'\n'+console).splitlines() if any(word in line for word in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object','STREAM_FIX_HOT_FAIL'))]
    ass=[line for line in text.splitlines() if '[libass]' in line]
    passed=rc==0 and not bad
    if is_smoke:passed=passed and any('track ready events=5' in line for line in ass) and any('fonts configured container=1' in line for line in ass)
    elif case in ('ui','ui-restore','nr-export','nr-restore'):
        marker={'ui':'LIST_PRESET_FLOW_UI_PASS','ui-restore':'LIST_PRESET_FLOW_RESTORE_PASS','nr-export':'NR_CONTROLS_UI_PASS','nr-restore':'NR_CONTROLS_RESTORE_PASS'}[case]
        passed=passed and marker in console and 'LIST_PRESET_FLOW_UI_FAIL' not in console and 'NR_CONTROLS_UI_FAIL' not in console
        if case=='ui':passed=passed and 'LIST_PRESET_FLOW_SCOPE_PASS' in console and bool(list((out/'screenshots').glob('*.png')))
        if case=='ui-restore':passed=passed and json.loads((profile/'qml-preferences.v1.json').read_text(encoding='utf8'))['fgPresets']==values['fgPresets']
        if case=='nr-export':
            worker_text='\n'.join(p.read_text(encoding='utf8',errors='replace') for p in (app/'logs').glob('export-worker-*.log'))
            frozen_export=[line for line in (text+'\n'+worker_text).splitlines() if '[export-frozen]' in line]
            passed=passed and 'LIST_PRESET_FLOW_EXPORT_SELECTED' in console and any('flow=0 content=3 ' in line for line in frozen_export)
            outputs=list((out/'outputs').glob('*.mp4'))
            passed=passed and len(outputs)==1 and len(list((out/'screenshots').glob('*.png')))>=4
            if outputs:
                probe_exe=shutil.which('ffprobe');assert probe_exe,'ffprobe is required'
                probe=json.loads(subprocess.check_output([probe_exe,'-v','error','-count_frames','-show_streams','-of','json',str(outputs[0])],timeout=30))
                stream=next(s for s in probe['streams'] if s['codec_type']=='video')
                passed=passed and stream['codec_name']=='hevc' and int(stream['nb_read_frames'])==60 and (int(stream['width']),int(stream['height']))==(3840,2160)
                (out/'export-probe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
    else:
        passed=passed and 'STREAM_FIX_HOT_PASS' in console and console.count('STREAM_FIX_FG_CASE')==4 and 'STREAM_FIX_NR_CASE' in console and bool(list((out/'screenshots').glob('*.png')))
    assert main.read_bytes()==original
    result={'passed':passed,'exit':rc,'seconds':round(time.monotonic()-begin,3),'args':args,
        'exeSha256':hashlib.sha256((app/'veyra_qml_ui.exe').read_bytes()).hexdigest(),
        'mainQmlSha256':hashlib.sha256(original).hexdigest(),'log':str(log),'engineLog':str(engine_log),
        'libass':ass[:8],'errors':bad[:20]}
    if case=='nr-export':result['frozenExport']=frozen_export
    log.with_suffix('.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('LOCAL APP',case,'PASS' if passed else 'FAIL',log)
    print('\n'.join(bad[:12] or [line for line in console.splitlines() if any(marker in line for marker in ('STREAM_FIX_','LIST_PRESET_FLOW_','NR_CONTROLS_'))]))
    subprocess.run(guard,check=True)
    raise SystemExit(0 if passed else 1)
BUILD = BASE/'build'/TASK
APP = BASE/'test-packages'/TASK/'Veyra-2.0.4-NVIDIA-win64-portable'
cases = {'xbox': 'veyra_xbox_tests', 'scene': 'veyra_scene_tests',
         'timing': 'veyra_live_timing_tests', 'codec': 'veyra_capture_compressed_tests',
         'repair': 'veyra_repair_contract_tests', 'chain': 'veyra_effect_chain_tests',
         'preset': 'veyra_preset_library_tests', 'legacy': 'veyra_repair_preset_tests',
         'i18n': 'veyra_ui_i18n_tests', 'hardware': 'veyra_effect_availability_tests',
         'vfg-settings': 'veyra_vfg_settings_tests', 'audio': 'veyra_capture_audio_tests',
         'temporal': 'veyra_nr_temporal_gpu_tests', 'correction': 'veyra_nr_correction_gpu_tests',
         'correction-fp16': 'veyra_nr_correction_gpu_tests'}
assert case in cases
LOG = BASE/'logs'/TASK/(label+'.log'); TMP = BASE/'tmp'/TASK/label
OUT = BASE/'tests'/TASK/label
for p in (LOG.parent, TMP, OUT): p.mkdir(parents=True, exist_ok=True)
guard = [sys.executable, '-B', str(ROOT/'scripts/acceptance/list-preset-flow-control.py')]
subprocess.run(guard, check=True)
exe = BUILD/(cases[case]+'.exe'); assert exe.is_file()
args = [str(exe)]
fixture = None
if case == 'legacy':args.append('presets/legacy-presets.v1')
if case == 'vfg-settings':args.append(str(OUT/'vfg-settings'))
if case == 'audio':args.append('--xbox-float-rtp')
if case.startswith('correction'):
    args += [str(BASE/'tests/nr-strength-protection-20261005/baseline/NrResidualComposite.dxil'),str(OUT/'shader')]
    if case.endswith('fp16'):args.append('--fp16')
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
