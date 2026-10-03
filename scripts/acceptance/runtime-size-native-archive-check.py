"""Independent official 7-Zip compatibility/CRC check, using only an E: build tool."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
archive=Path(sys.argv[1]).resolve();archive.relative_to((BASE/'test-packages'/TASK).resolve())
label=sys.argv[2];assert label.replace('-','').isalnum()
tool=BASE/'deps/7zip-26.03-runtime-size-20261004/x64/7za.exe'
tmp=BASE/'tmp'/TASK/'native-archive';tmp.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env['TEMP']=env['TMP']=str(tmp)
log=BASE/'logs'/TASK/(label+'.log')
with log.open('xb') as out:
    code=subprocess.run([str(tool),'t',str(archive),'-bb0','-bd','-mmt=2','-sccUTF-8'],cwd=tmp,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=290).returncode
text=log.read_text(encoding='utf8',errors='replace');print(text[-1100:])
assert code==0 and 'Everything is Ok' in text,text[-4000:]
result={'archive':str(archive),'cli':str(tool),'cliSha256':hashlib.sha256(tool.read_bytes()).hexdigest(),'code':code,'status':'pass','log':str(log),'toolProvenance':'7zip-tool-provenance.json'}
(BASE/'logs'/TASK/(label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('OFFICIAL 7ZIP CRC/COMPATIBILITY PASS',archive)
