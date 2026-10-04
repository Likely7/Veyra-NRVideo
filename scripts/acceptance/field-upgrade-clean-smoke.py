"""Verify the final ZIP, then exercise six pages in two renderers after extraction."""
import json, os, subprocess, sys, zipfile
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='field-upgrade-20261003';LABEL='2.0.2-field-20261003'
ROOT=Path(__file__).resolve().parents[2]
archive=BASE/'test-packages'/TASK/('Veyra-'+LABEL+'-win64-portable.zip')
verify=BASE/'verify'/TASK;verify.mkdir(parents=True,exist_ok=False)
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():(verify/name).resolve().relative_to(verify.resolve())
    z.extractall(verify)
app=verify/('Veyra-'+LABEL+'-win64-portable')
logdir=BASE/'logs'/TASK/'clean-smoke';logdir.mkdir(parents=True,exist_ok=False)
tmp=BASE/'tmp'/TASK/'clean-smoke';tmp.mkdir(parents=True,exist_ok=False)
manifest=json.loads((app/'package-manifest.json').read_text(encoding='utf8'))
import hashlib
for row in manifest['files']:
    f=app/row['path']
    assert f.stat().st_size==row['size'] and hashlib.sha256(f.read_bytes()).hexdigest()==row['sha256'],row['path']
assert not any(f.suffix=='.addon64' or 'lmxxf-test-runtime' in str(f) for f in app.rglob('*'))
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
   test.appWindow.page=test.pages[test.step]
   test.waiting=true
   shot.restart()
  }
 }
 Timer { id: shot; interval: 350; onTriggered: {
  test.appWindow.contentItem.grabToImage(result=>{
   const name=test.pages[test.step]
   if(!result.saveToFile(test.evidence+"/"+name+".png")){console.error("CLEAN_SMOKE_FAIL screenshot");Qt.quit();return}
   console.log("CLEAN_SMOKE_PAGE",name,test.appWindow.color,test.appWindow.width,test.appWindow.height)
   test.step++;test.waiting=false
  })
 } }
}'''
qml=tmp/'clean-smoke.qml';qml.write_text(source,encoding='utf8')
main=app/'qml/Veyra/Main.qml';original=main.read_bytes()
results=[]
try:
    for mode in ('gpu','software'):
        evidence=verify/(mode+'-evidence');evidence.mkdir()
        original_text=original.decode('utf8');at=original_text.rfind('}')
        loader='\nLoader { source: '+json.dumps(qml.as_uri())+'; onLoaded: { item.evidence='+json.dumps(str(evidence))+'; item.expectSoftware='+str(mode=='software').lower()+' } }\n'
        main.write_text(original_text[:at]+loader+original_text[at:],encoding='utf8')
        env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logdir/(mode+'-app.log')),
            QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
        for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI'):env.pop(key,None)
        if mode=='gpu':env['VEYRA_UI_RHI']='d3d12'
        args=['.\\veyra_qml_ui.exe','--size','1280x720','--reduced-motion','--data-dir',str(verify/(mode+'-profile')),'--exit-after','27000']
        if mode=='software':args.append('--obs-game-capture')
        with (logdir/(mode+'-console.log')).open('xb') as log:
            code=subprocess.run(args,executable=str(app/'veyra_qml_ui.exe'),cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=40).returncode
        console=(logdir/(mode+'-console.log')).read_text(encoding='utf8',errors='replace')
        assert code==0 and 'CLEAN_SMOKE_PASS' in console and 'CLEAN_SMOKE_FAIL' not in console,console[-3000:]
        assert len(list(evidence.glob('*.png')))==6
        from PIL import Image
        pixels=[]
        for file in sorted(evidence.glob('*.png')):
            im=Image.open(file).convert('RGBA');p=im.getpixel((min(15,im.width-1),min(80,im.height-1)))
            assert p[3]==255 and max(p[:3])<160,(file,p)
            pixels.append({'page':file.stem,'pixel':p,'size':im.size})
        results.append({'renderer':mode,'exit':code,'pages':pixels})
        print('CLEAN SMOKE PASS',mode,'six pages',flush=True)
finally:main.write_bytes(original)
(logdir/'summary.json').write_text(json.dumps({'archive':str(archive),'filesVerified':len(manifest['files']),'results':results},indent=2),encoding='utf8')
print('CLEAN PACKAGE PASS',len(manifest['files']),'files;',verify,flush=True)
