from pathlib import Path, PurePosixPath
import ctypes, hashlib, json, os, stat, subprocess, time, zipfile

BASE=Path('E:/项目/Veyra'); TASK='publish-2.0.5-20261007'
PACK=BASE/'releases'/TASK/'packages'; LOG=BASE/'logs'/TASK
receipt=json.loads((PACK/'DELIVERY.json').read_text(encoding='utf8'))
results=[]
for row in receipt:
    vendor=row['vendor']; destination=BASE/'verify'/TASK/vendor
    assert destination.resolve().is_relative_to((BASE/'verify'/TASK).resolve()) and not destination.exists()
    archive=Path(row['archive']); assert hashlib.sha256(archive.read_bytes()).hexdigest()==row['archiveSha256']
    with zipfile.ZipFile(archive) as z:
        assert len(z.infolist())<5000 and sum(i.file_size for i in z.infolist())<5*1024**3
        for entry in z.infolist():
            path=PurePosixPath(entry.filename)
            assert not path.is_absolute() and '..' not in path.parts and ':' not in entry.filename
            assert not stat.S_ISLNK(entry.external_attr>>16)
            assert (destination/entry.filename).resolve().is_relative_to(destination.resolve())
        z.extractall(destination)
    app=destination/Path(row['candidate']).name
    manifest=json.loads((app/'package-manifest.json').read_text(encoding='utf8'))
    for item in manifest['files']:
        assert hashlib.sha256((app/item['path']).read_bytes()).hexdigest()==item['sha256'],item['path']
    out=BASE/'tests'/TASK/('cold-'+vendor); tmp=BASE/'tmp'/TASK/('cold-'+vendor)
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    engine=LOG/('cold-'+vendor+'-engine.log'); console=LOG/('cold-'+vendor+'.log')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
    windows=Path(env['WINDIR'])
    env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_LOG_FILE=str(engine),
        QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',
        PATH=os.pathsep.join(map(str,(windows/'System32',windows,windows/'System32/Wbem'))))
    args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(out/'profile'),'--page','pro','--size','1280x800',
        '--obs-game-capture','--exit-after','8000',str(BASE/'tests/field-fixes-20261005/media/fx-embedded.mkv')]
    begin=time.monotonic(); modules=[]
    with console.open('xb') as log:
        process=subprocess.Popen(args,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            time.sleep(3)
            kernel=ctypes.WinDLL('kernel32',use_last_error=True); psapi=ctypes.WinDLL('psapi',use_last_error=True)
            kernel.OpenProcess.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];kernel.OpenProcess.restype=ctypes.c_void_p
            kernel.CloseHandle.argtypes=[ctypes.c_void_p]
            psapi.EnumProcessModulesEx.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p),ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_ulong]
            psapi.GetModuleFileNameExW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_ulong]
            handle=kernel.OpenProcess(0x0410,False,process.pid);assert handle
            try:
                items=(ctypes.c_void_p*512)(); needed=ctypes.c_ulong()
                assert psapi.EnumProcessModulesEx(handle,items,ctypes.sizeof(items),ctypes.byref(needed),3)
                for i in range(min(512,needed.value//ctypes.sizeof(ctypes.c_void_p))):
                    buffer=ctypes.create_unicode_buffer(32768)
                    if psapi.GetModuleFileNameExW(handle,items[i],buffer,len(buffer)):
                        path=Path(buffer.value)
                        if path.name.startswith(('Qt6','avcodec-','avformat-','avutil-','swscale-','swresample-')):
                            assert path.resolve().is_relative_to(app.resolve()),str(path)
                            modules.append(str(path))
            finally:kernel.CloseHandle(handle)
            rc=process.wait(timeout=50)
        except BaseException:
            process.kill();process.wait(timeout=10);raise
    text=engine.read_text(encoding='utf8',errors='replace')+'\n'+console.read_text(encoding='utf8',errors='replace')
    bad=[line for line in text.splitlines() if any(word in line for word in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object'))]
    assert rc==0 and not bad and len(modules)>10 and 'track ready events=5' in text and 'fonts configured container=1' in text,bad[:12]
    result={'vendor':vendor,'passed':True,'exit':rc,'seconds':round(time.monotonic()-begin,3),'app':str(app),
        'args':args,'archiveSha256':row['archiveSha256'],'executableSha256':row['exeSha256'],
        'ownQtAndCodecModulePaths':modules,'console':str(console),'engine':str(engine)}
    results.append(result);print('COLD ZIP START PASS',vendor,len(modules),'modules from extracted app',flush=True)
    (LOG/'cold-verify-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
subprocess.run(['py','-3.11','-B','scripts/acceptance/publish-2.0.5-control.py'],cwd=BASE/'worktrees'/TASK,check=True)
