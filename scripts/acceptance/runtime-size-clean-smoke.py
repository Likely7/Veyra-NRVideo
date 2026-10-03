"""Final 7z extraction identity, native VFG, and six pages in both renderers."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
from PIL import Image
BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004';LABEL='2.0.2-slim-20261004';ROOT=Path(__file__).resolve().parents[2]
archive=BASE/'test-packages'/TASK/('Veyra-'+LABEL+'-win64-portable.7z')
app=Path(sys.argv[1]).resolve();app.relative_to((BASE/'verify'/TASK).resolve());assert app.is_dir()
verify=app.parent;logs=BASE/'logs'/TASK/'clean-smoke';tmp=BASE/'tmp'/TASK/'clean-smoke'
for p in (logs,tmp):p.mkdir(parents=True,exist_ok=False)
manifest=json.loads((app/'package-manifest.json').read_text(encoding='utf8'))
def verifyFiles():
    for row in manifest['files']:
        f=app/row['path'];assert f.stat().st_size==row['size'] and hashlib.sha256(f.read_bytes()).hexdigest()==row['sha256'],row['path']
verifyFiles()
assert not any(f.suffix in ('.pyd','.addon64','.pdb','.lib','.whl') for f in app.rglob('*') if f.is_file())
command=[sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-ui.py'),'ui-package-final','package',str(app)]
with (logs/'vfg-native-ui.log').open('xb') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=290,check=True)
source='''import QtQuick
Item {
 id: test
 property var appWindow: Window.window
 property string evidence: ""
 property bool expectSoftware: false
 property int step: 0
 property bool waiting: false
 property int ticks: 0
 property var pages: ["home","pro","node","exp","set","min"]
 Timer { id: timer; interval: 250; repeat: true; running: true
  onTriggered: {
   if(++test.ticks>100){console.error("CLEAN_SMOKE_FAIL timeout");Qt.quit();return}
   if(!test.evidence.length||test.ticks<8||test.waiting)return
   if(vySoftwareUi!==test.expectSoftware){console.error("CLEAN_SMOKE_FAIL renderer mismatch");Qt.quit();return}
   if(test.step>=test.pages.length){console.log("CLEAN_SMOKE_PASS",test.expectSoftware);timer.stop();Qt.quit();return}
   test.appWindow.page=test.pages[test.step];test.waiting=true;shot.restart()
  }
 }
 Timer { id: shot; interval: 350; onTriggered: {
  test.appWindow.contentItem.grabToImage(result=>{
   if(!result.saveToFile(test.evidence+"/"+test.pages[test.step]+".png")){console.error("CLEAN_SMOKE_FAIL screenshot");Qt.quit();return}
   test.step++;test.waiting=false
  })
 } }
}'''
qml=tmp/'pages.qml';qml.write_text(source,encoding='utf8');main=app/'qml/Veyra/Main.qml';original=main.read_bytes();results=[]
try:
    for mode in ('gpu','software'):
        evidence=verify/(mode+'-evidence');evidence.mkdir();text=original.decode('utf8');at=text.rfind('}')
        loader='\nLoader { source: '+json.dumps(qml.as_uri())+'; onLoaded: { item.evidence='+json.dumps(str(evidence))+'; item.expectSoftware='+str(mode=='software').lower()+' } }\n'
        main.write_text(text[:at]+loader+text[at:],encoding='utf8')
        env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/(mode+'-app.log')),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
        for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','VEYRA_VFG_RUNTIME'):env.pop(key,None)
        env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
        if mode=='gpu':env['VEYRA_UI_RHI']='d3d12'
        args=['.\\veyra_qml_ui.exe','--size','1280x720','--reduced-motion','--data-dir',str(verify/(mode+'-profile')),'--exit-after','27000']
        if mode=='software':args.append('--obs-game-capture')
        with (logs/(mode+'-console.log')).open('xb') as log:code=subprocess.run(args,executable=str(app/'veyra_qml_ui.exe'),cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=40).returncode
        console=(logs/(mode+'-console.log')).read_text(encoding='utf8',errors='replace');assert code==0 and 'CLEAN_SMOKE_PASS' in console and 'CLEAN_SMOKE_FAIL' not in console,console[-3000:]
        files=sorted(evidence.glob('*.png'));assert len(files)==6;pixels=[]
        for file in files:
            im=Image.open(file).convert('RGBA');pixel=im.getpixel((15,80));assert pixel[3]==255 and max(pixel[:3])<160,(file,pixel)
            pixels.append({'page':file.stem,'pixel':pixel,'size':im.size})
        results.append({'renderer':mode,'exit':code,'pages':pixels});print('CLEAN SMOKE PASS',mode,flush=True)
finally:main.write_bytes(original)
verifyFiles()
(logs/'summary.json').write_text(json.dumps({'archive':str(archive),'filesVerified':len(manifest['files']),'portableVfgUi':str(BASE/'logs'/TASK/'ui-package-final/summary.json'),'results':results},indent=2),encoding='utf8')
print('CLEAN VFG PACKAGE PASS',len(manifest['files']),verify,flush=True)
