"""Three real NR styles on one paused frame; immutable input package copy."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005'
parser=argparse.ArgumentParser()
parser.add_argument('label');parser.add_argument('--app',required=True);parser.add_argument('--media',required=True)
parser.add_argument('--scenarios')
parser.add_argument('--manual',action='store_true')
a=parser.parse_args();assert a.label.replace('-','').isalnum()
source=Path(a.app);media=Path(a.media)
assert source.is_dir() and media.is_file()
assert source.resolve().is_relative_to(BASE.resolve()) and media.resolve().is_relative_to(BASE.resolve())
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/nr-protection-control.py'),'guard'],check=True)
OUT=BASE/'tests'/TASK/('styles-'+a.label);LOG=BASE/'logs'/TASK/('styles-'+a.label);TMP=BASE/'tmp'/TASK/('styles-'+a.label)
for p in (OUT,LOG,TMP):p.mkdir(parents=True,exist_ok=False)
(OUT/'screenshots').mkdir()
APP=OUT/'app';shutil.copytree(source,APP,ignore=shutil.ignore_patterns('logs','outputs','screenshots'))
scenarios=json.loads(Path(a.scenarios).read_text(encoding='utf8')) if a.scenarios else [
    {'label':f'style{s}-{mode}', 'style':s,'enabled':mode=='auto','automatic':True}
    for s in range(3) for mode in ('raw','auto')]
if a.manual:
    keys=('hueProtection','chromaProtection','highlightProtection','localCompression','temporalStability',
          'neutralProtection','colorRetention','luminanceRetention','shadowProtection')
    for key in ('none','neutralProtection','colorRetention','luminanceRetention','shadowProtection'):
        values=dict.fromkeys(keys,0)
        if key!='none':values[key]=1
        scenarios.append({'label':'style1-manual-'+key,'style':1,'enabled':True,'automatic':False,'values':values})
    scenarios.append({'label':'style1-auto-zero','style':1,'enabled':True,'automatic':True,'values':{'correctionAmount':0}})
main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();text=original.decode('utf8')
loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/nr-protection-styles.qml').as_uri())+'; onLoaded: {item.media='+json.dumps(str(media))+';item.evidence='+json.dumps(str(OUT))+';item.scenarios='+json.dumps(scenarios)+';item.fixedImage='+str(media.suffix.lower() in ('.png','.jpg','.jpeg')).lower()+'} }\n'
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),VEYRA_LOG_FILE=str(LOG/'veyra.log'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','VEYRA_TEST_IGNORE_RTSS','VEYRA_TEST_REJECT_NR_STYLE2'):env.pop(key,None)
args=[str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x900','--reduced-motion','--obs-game-capture','--data-dir',str(OUT/'profile'),'--exit-after','240000']
try:
    pos=text.rfind('}');main.write_text(text[:pos]+loader+text[pos:],encoding='utf8')
    with (LOG/'console.log').open('xb') as log:
        rc=subprocess.run(args,cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=260).returncode
finally:main.write_bytes(original)
console=(LOG/'console.log').read_text(encoding='utf8',errors='replace')
assert rc==0 and 'NR_STYLES_PASS' in console and 'NR_STYLES_FAIL' not in console,console[-5000:]
images=sorted((OUT/'screenshots').glob('*.png'));labels=['source']+[s['label'] for s in scenarios]
assert len(images)==len(labels),(len(images),len(labels))
records=[]
for label,path in zip(labels,images):
    target=path.with_name(label+'.png');path.rename(target)
    records.append({'label':label,'path':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
receipt={'passed':True,'package':str(source),'exeSha256':hashlib.sha256((source/'veyra_qml_ui.exe').read_bytes()).hexdigest(),'media':str(media),'scenarios':scenarios,'captures':records}
(LOG/'result.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print('NR styles PASS',OUT,flush=True)
