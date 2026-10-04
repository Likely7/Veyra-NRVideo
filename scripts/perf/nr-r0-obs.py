"""Actual OBS game-capture regression in a fresh portable instance.

Only the unique, owned Veyra title is selected. No display/mic/system audio,
network stream, user OBS configuration, or parallel timing test is involved.
API definitions: obsproject/obs-websocket docs/generated/protocol.md (RPC 1).
Portable/first-run keys: obsproject/obs-studio 32.1.2 frontend/OBSApp.cpp and
frontend/widgets/OBSBasic.cpp. This script uses the official API, not UI input.
"""
from pathlib import Path
import base64, ctypes, hashlib, importlib.util, json, os, secrets, shutil, socket, statistics, struct, subprocess, sys, time
import psutil
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
spec = importlib.util.spec_from_file_location('matrix', ROOT/'scripts/perf/nr-matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)

class ObsRpc:
    def __init__(self, port, password):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=10)
        self.buffer = b''
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        self.socket.sendall((f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                             f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        while b'\r\n\r\n' not in self.buffer:
            self.buffer += self.socket.recv(4096)
        headers, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
        assert b' 101 ' in headers.split(b'\r\n')[0]
        expected = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert expected.lower() in headers.lower()
        hello = self.receive()
        assert hello['op'] == 0
        auth = hello['d']['authentication']
        secret = base64.b64encode(hashlib.sha256((password+auth['salt']).encode()).digest()).decode()
        response = base64.b64encode(hashlib.sha256((secret+auth['challenge']).encode()).digest()).decode()
        self.send({'op':1, 'd':{'rpcVersion':1, 'authentication':response, 'eventSubscriptions':0}})
        assert self.receive()['op'] == 2
        self.counter = 0

    def take(self, count):
        while len(self.buffer) < count:
            data = self.socket.recv(max(4096, count-len(self.buffer)))
            if not data:
                raise EOFError('OBS websocket closed')
            self.buffer += data
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def frame(self, data, opcode=1):
        mask = secrets.token_bytes(4)
        length = len(data)
        header = bytes([0x80|opcode, 0x80|length]) if length < 126 else (bytes([0x80|opcode, 0x80|126])+struct.pack('!H', length) if length < 65536 else bytes([0x80|opcode, 0x80|127])+struct.pack('!Q', length))
        self.socket.sendall(header+mask+bytes(v ^ mask[i%4] for i,v in enumerate(data)))

    def send(self, data):
        self.frame(json.dumps(data).encode())

    def receive(self):
        chunks = []
        while True:
            a,b = self.take(2)
            length = b & 127
            if length == 126: length = struct.unpack('!H', self.take(2))[0]
            if length == 127: length = struct.unpack('!Q', self.take(8))[0]
            assert length < 2000000
            mask = self.take(4) if b & 128 else None
            data = self.take(length)
            if mask: data = bytes(v ^ mask[i%4] for i,v in enumerate(data))
            if a & 15 == 8: raise EOFError('OBS websocket close frame')
            if a & 15 == 9: self.frame(data, 10); continue
            if a & 15 == 10: continue
            assert a & 15 in (0,1)
            chunks.append(data)
            if a & 128: return json.loads(b''.join(chunks))

    def call(self, kind, data=None):
        deadline = time.monotonic()+15
        while True:
            self.counter += 1
            request_id = str(self.counter)
            self.send({'op':6, 'd':{'requestId':request_id, 'requestType':kind, 'requestData':data or {}}})
            while True:
                response = self.receive()
                if response['op'] == 7 and response['d']['requestId'] == request_id:
                    result = response['d']; break
            if result['requestStatus']['code'] == 207 and time.monotonic() < deadline:
                # Websocket can identify before the portable profile is ready.
                time.sleep(.25); continue
            assert result['requestStatus']['result'], (kind, result['requestStatus'])
            return result.get('responseData', {})

def close_owned(process, expected):
    if process is None or process.poll() is not None: return
    assert Path(psutil.Process(process.pid).exe()).resolve() == expected.resolve()
    from ctypes import wintypes
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    def close(hwnd, unused):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == process.pid: user.PostMessageW(hwnd, 0x10, 0, 0)
        return True
    callback = callback_type(close)
    user.EnumWindows(callback, 0)
    try: process.wait(timeout=10)
    except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=8)

def config_manifest():
    folder = Path(os.environ['APPDATA'])/'obs-studio'
    return {p.relative_to(folder).as_posix():matrix.digest(p) for p in folder.rglob('*')
            if p.is_file() and p.suffix.lower() in ('.ini','.json')}

def run():
    variant, label = sys.argv[1:3]
    overrides = [arg.split('=',1)[1] for arg in sys.argv[3:] if arg.startswith('--milestone-exe=')]
    assert len(overrides) <= 1
    switches = {arg for arg in sys.argv[3:] if arg.startswith('--') and not arg.startswith('--milestone-exe=')}
    assert switches <= {'--baseline-qml','--paused-present-off','--pre-exit-exe','--nr-residual-off'}
    modes = tuple(arg for arg in sys.argv[3:] if not arg.startswith('--')) or ('gpu','compat')
    assert len(set(modes)) == len(modes) and all(mode in ('gpu','compat') for mode in modes)
    assert label.replace('-', '').isalnum()
    matrix.assert_gpu_tests_idle()
    assert not any((p.info['name'] or '').lower() == 'obs64.exe' for p in psutil.process_iter(['name'])), 'Existing OBS process; do not disturb it'
    logs, tmp, test = (BASE/name/TASK/label for name in ('logs','tmp','tests'))
    for folder in (logs,tmp,test): folder.mkdir(exist_ok=False)
    installed = Path('E:/App/obs-studio')
    portable = test/'obs-portable'
    portable.mkdir()
    # Hardlink only read-only installation payload. All configuration is fresh.
    for folder in ('bin','data','obs-plugins'):
        shutil.copytree(installed/folder, portable/folder, copy_function=os.link)
    (portable/'portable_mode.txt').write_text('', encoding='utf-8')
    config = portable/'config/obs-studio'
    config.mkdir(parents=True)
    (config/'global.ini').write_text('[General]\nEnableAutoUpdates=false\nInfoIncrement=999999\n[Video]\nRenderer=Direct3D 11\n', encoding='utf-8')
    (config/'user.ini').write_text('[General]\nFirstRun=true\n[Basic]\nProfile=VeyraR0\nProfileDir=VeyraR0\n[BasicWindow]\nPreviewEnabled=false\nSysTrayEnabled=true\n', encoding='utf-8')
    profile = config/'basic/profiles/VeyraR0'
    profile.mkdir(parents=True)
    (profile/'basic.ini').write_text('[General]\nName=VeyraR0\n[Video]\nBaseCX=1280\nBaseCY=800\nOutputCX=1280\nOutputCY=800\nFPSType=0\nFPSCommon=30\n[Audio]\nSampleRate=48000\nChannelSetup=Stereo\n[Output]\nMode=Simple\n[SimpleOutput]\nStreamEncoder=x264\nPreset=ultrafast\nVBitrate=2500\nRecQuality=Stream\nRecFormat2=mkv\nFilePath='+str(test/'recordings').replace('\\','/')+'\n', encoding='utf-8')
    ws_config = config/'plugin_config/obs-websocket/config.json'
    ws_config.parent.mkdir(parents=True)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0)); port = reservation.getsockname()[1]
    password = secrets.token_urlsafe(32)
    settings = {'first_load':False,'server_enabled':True,'server_port':port,'alerts_enabled':False,'auth_required':True,'server_password':password}
    ws_config.write_text(json.dumps(settings), encoding='utf-8')
    app = test/'veyra-app'
    shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable', app, copy_function=matrix.copy_dependency)
    executable = BASE/'build'/TASK/variant/'veyra_qml_ui.exe'
    if '--pre-exit-exe' in switches:
        executable = BASE/'tests'/TASK/'app-B2d-R0-normal-v1/veyra_qml_ui.exe'
        assert matrix.digest(executable) == '8c2276d68d15c2e4466befcf367439b7efc42c68defd5ef93182398c9e8d8a03'
    if overrides:
        assert '--pre-exit-exe' not in switches
        executable = Path(overrides[0]).resolve()
        assert executable.is_relative_to((BASE/'tests'/TASK).resolve()) and executable.name == 'veyra_qml_ui.exe' and executable.is_file()
    shutil.copy2(executable, app/'veyra_qml_ui.exe')
    baseline_qml = variant == 'A' or '--baseline-qml' in switches
    if not baseline_qml: shutil.copytree(ROOT/'qml', app/'qml', dirs_exist_ok=True)
    shutil.copy2(ROOT/'scripts/perf/nr-r0-obs.qml', app/'qml/Veyra/ObsProbe.qml')
    actions = test/'actions.json'
    main = app/'qml/Veyra/Main.qml'
    source = main.read_text(encoding='utf-8'); pos = source.rfind('}')
    main.write_text(source[:pos]+'\n Loader { source: "ObsProbe.qml"; onLoaded: item.commandFile="'+actions.as_uri()+'" }\n'+source[pos:], encoding='utf-8')
    payload = {p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
    user_config_before = config_manifest()
    identity = {'playerPayload':payload,'obsExeSha256':matrix.digest(portable/'bin/64bit/obs64.exe'),
                'installedObsExeSha256':matrix.digest(installed/'bin/64bit/obs64.exe'),'userConfigBefore':user_config_before,
                'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'gpuCompetition':False,'baselineQml':baseline_qml,
                'pausedPresentReuseDisabled':'--paused-present-off' in switches,'nrResidualReuseDisabled':'--nr-residual-off' in switches,
                'executableSource':str(executable),
                'recordingPurpose':'Game capture compatibility, not timing or pressure'}
    (logs/'artifacts.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8')
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','OBS_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp))
    if '--paused-present-off' in switches: env['VEYRA_TEST_DISABLE_PAUSED_PRESENT_REUSE']='1'
    if '--nr-residual-off' in switches: env['VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE']='1'
    obs, player, rpc = None,None,None
    rows = []
    try:
        with (logs/'obs-console.log').open('xb') as console:
            obs = subprocess.Popen([str(portable/'bin/64bit/obs64.exe'),'--portable','--disable-updater','--only-bundled-plugins','--disable-missing-files-check','--minimize-to-tray'], cwd=portable/'bin/64bit', env=env, stdout=console, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+35
        while True:
            assert obs.poll() is None, 'Owned OBS exited before API became available'
            try: rpc = ObsRpc(port,password); break
            except (OSError, EOFError):
                if time.monotonic() > deadline: raise
                time.sleep(.25)
        assert any(c.laddr.port == port and c.status == psutil.CONN_LISTEN for c in psutil.Process(obs.pid).net_connections(kind='inet'))
        identity['actualObsVersion'] = rpc.call('GetVersion')['obsVersion']
        assert identity['actualObsVersion'] == '32.1.2'
        # Remove fresh defaults before recording. Never capture personal audio.
        for entry in rpc.call('GetInputList')['inputs']:
            rpc.call('RemoveInput', {'inputName':entry['inputName']})
        rpc.call('SetVideoSettings',{'baseWidth':1280,'baseHeight':800,'outputWidth':1280,'outputHeight':800,'fpsNumerator':30,'fpsDenominator':1})
        (test/'recordings').mkdir()
        rpc.call('SetRecordDirectory', {'recordDirectory':str(test/'recordings')})
        rpc.call('CreateScene',{'sceneName':'VeyraR0'})
        rpc.call('SetCurrentProgramScene',{'sceneName':'VeyraR0'})
        for mode in modes:
            title = f'Veyra R0 OBS {label} {mode}'
            command = {'sequence':0,'action':'start','title':title,'media':str(matrix.SOURCES['M1']).replace('\\','/')}
            def action(value):
                command.update(sequence=command['sequence']+1,action=value)
                pending = actions.with_suffix('.new'); pending.write_text(json.dumps(command),encoding='utf-8'); pending.replace(actions)
            action('start')
            data = test/(mode+'-profile'); data.mkdir()
            (data/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','prewarmEnhancement':False,'gpuPriority':'normal'}),encoding='utf-8')
            player_env = {**env,'VEYRA_LOG_FILE':str(logs/(mode+'-player.log')),'QML_DISABLE_DISK_CACHE':'1','QML_XHR_ALLOW_FILE_READ':'1','QT_FORCE_STDERR_LOGGING':'1'}
            args = [str(app/'veyra_qml_ui.exe'),'--data-dir',str(data),'--page','pro','--size','1280x800','--exit-after','170000']
            if mode == 'compat': args.append('--obs-game-capture')
            with (logs/(mode+'-console.log')).open('xb') as console:
                player = subprocess.Popen(args,cwd=app,env=player_env,stdout=console,stderr=subprocess.STDOUT)
            deadline = time.monotonic()+35
            while True:
                assert player.poll() is None, 'Owned player exited before NR playback'
                log_file = logs/(mode+'-player.log')
                samples = [line.split('OBS_UI_SAMPLE ',1)[1] for line in log_file.read_text(encoding='utf-8',errors='replace').splitlines()
                    if 'OBS_UI_SAMPLE ' in line] if log_file.exists() else []
                if samples:
                    latest = json.loads(samples[-1])
                    if latest['nr'] and latest['position'] > 1 and not latest['paused'] and not latest['failed']: break
                assert time.monotonic() < deadline, 'Actual enhanced playback did not become ready'
                time.sleep(.25)
            input_name = 'VeyraOwned-'+mode
            rpc.call('CreateInput',{'sceneName':'VeyraR0','inputName':input_name,'inputKind':'game_capture','inputSettings':{'capture_mode':'window','capture_cursor':False,'capture_overlays':True,'allow_transparency':False},'sceneItemEnabled':True})
            items = rpc.call('GetInputPropertiesListPropertyItems',{'inputName':input_name,'propertyName':'window'})['propertyItems']
            matches = [item for item in items if title in str(item['itemValue'])]
            assert len(matches) == 1, ('Unique owned capture window not found', mode, matches)
            rpc.call('SetInputSettings',{'inputName':input_name,'inputSettings':{'window':matches[0]['itemValue'],'priority':0},'overlay':True})
            rpc.call('StartRecord')
            shots = []
            for stage in ('playing','pause','resume','resize','fullscreen','windowed'):
                if stage != 'playing': action(stage)
                time.sleep(4)
                shot = test/(mode+'-'+stage+'.png')
                rpc.call('SaveSourceScreenshot',{'sourceName':input_name,'imageFormat':'png','imageFilePath':str(shot),'imageWidth':1280,'imageHeight':800})
                image = Image.open(shot).convert('RGB')
                crop = image.crop((200,160,1080,600))
                variation = ImageStat.Stat(crop).stddev
                assert max(variation) > 12, ('Capture lacks real picture',mode,stage,variation)
                shots.append({'stage':stage,'image':str(shot),'sha256':matrix.digest(shot),'centerStddev':variation})
            stopped = rpc.call('StopRecord')
            clip = Path(stopped['outputPath'])
            assert clip.resolve().is_relative_to((test/'recordings').resolve()) and clip.stat().st_size > 10000
            stats = rpc.call('GetStats')
            rpc.call('RemoveInput',{'inputName':input_name})
            action('quit'); player.wait(timeout=12)
            text = (logs/(mode+'-player.log')).read_text(encoding='utf-8',errors='replace')
            assert player.returncode == 0 and 'OBS_UI_PASS' in text
            assert not any(value in text for value in ('OBS_UI_FAIL','[ERROR]','[FATAL]','ReferenceError:','TypeError:'))
            row = {'mode':mode,'command':args,'recording':str(clip),'recordingSha256':matrix.digest(clip),'shots':shots,'obsStats':stats,'passed':True}
            rows.append(row); (logs/'completed.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
            print('R0_OBS_CAPTURE_PASS',mode,len(shots),flush=True)
            player = None
        assert all(matrix.digest(app/name)==sha for name,sha in payload.items())
        (logs/'summary.json').write_text(json.dumps({'identity':identity,'runs':rows,'passed':True,'pressure':False},ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        if rpc:
            try:
                if rpc.call('GetRecordStatus')['outputActive']: rpc.call('StopRecord')
            except (OSError,EOFError,AssertionError): pass
            rpc.socket.close()
        close_owned(player,app/'veyra_qml_ui.exe')
        close_owned(obs,portable/'bin/64bit/obs64.exe')
        settings.update(server_enabled=False,server_password='')
        ws_config.write_text(json.dumps(settings),encoding='utf-8')
        user_config_after = config_manifest()
        unchanged = user_config_before == user_config_after
        (logs/'cleanup.json').write_text(json.dumps({'userConfigUnchanged':unchanged,'ownedObsStopped':obs is None or obs.poll() is not None,
            'ownedPlayerStopped':player is None or player.poll() is not None,'ephemeralServerDisabled':True},indent=2),encoding='utf-8')
        assert unchanged, 'User OBS config changed unexpectedly'
    matrix.assert_gpu_tests_idle()
    print('R0_OBS_COMPLETE',len(rows),flush=True)

if __name__ == '__main__': run()
