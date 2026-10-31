"""Audit/stage a local RX7000 runtime; does not publish or alter publisher locks.
Only the declared NR subset is admitted. Real driver/HIP DLLs, game proxies,
OptiScaler, ReShade, SR/RR/FG modules and SDKs are never copied by this script.
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('dependency-root','components','build-dir','app','report'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();base=Path('E:/项目/Veyra').resolve()
for x in (a.dependency_root,a.build_dir,a.app,a.report):x.resolve().relative_to(base)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
source=a.dependency_root/'runtime';manifest=json.loads(a.components.read_text('utf8'))
assert manifest['archiveSha256']=='779b7d375bbe6d2419bcbd68e3d229c84acf09c74206837cfb0e12a253da1b00'
for row in manifest['files']:
    path=source/row['path'];path.resolve().relative_to(source.resolve())
    assert path.stat().st_size==row['size'] and sha(path)==row['sha256'],row['path']
nr=source/'nvngx_dlssnr.dll'
assert nr.stat().st_size==266424944 and sha(nr)=='2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f'
env={k:v for k,v in os.environ.items() if k.upper()!='PSMODULEPATH'}
env['VEYRA_RDNA3_AUDIT_FILE']=str(nr)
script="$p=$env:VEYRA_RDNA3_AUDIT_FILE; $s=Get-AuthenticodeSignature -LiteralPath $p; $v=(Get-Item -LiteralPath $p).VersionInfo; [pscustomobject]@{signature=[string]$s.Status;signer=$s.SignerCertificate.Subject;version=\"$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)\"}|ConvertTo-Json"
identity=json.loads(subprocess.check_output(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],env=env,timeout=30).decode('utf-8-sig'))
assert identity['signature']=='Valid' and 'NVIDIA' in identity['signer'] and identity['version']=='310.9.16.0'
worker=a.build_dir/'veyra_amd_nr_worker.exe';assert worker.is_file()
assert b'CONTRACT TEST WORKER' not in worker.read_bytes(),'never stage the test worker as a product'
target=a.app/'runtime/amd-nr-rdna3';target.mkdir(parents=True,exist_ok=True)
rows=[]
for row in manifest['files']:
    rel=row['path'];src=source/rel;dst=target/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if dst.exists():assert sha(dst)==row['sha256'],'refuse overwriting existing runtime assets: '+rel
    elif rel.startswith('cache/') or src.suffix.lower() not in {'.dll','.hsaco'}:shutil.copyfile(src,dst)
    else:os.link(src,dst) # read-only components; writable cache is a fresh file
    rows.append(dict(row,classification='proprietary-NVIDIA-original' if rel=='nvngx_dlssnr.dll' else 'pretranslated-cache' if rel.endswith('.db') else 'open-source-component-or-notice'))
for name in ('ZLUDA-LICENSE-MIT','ZLUDA-LICENSE-APACHE'):
    src=source/'licenses'/name;dst=target/'licenses'/name;dst.parent.mkdir(exist_ok=True)
    if not dst.exists():shutil.copyfile(src,dst)
    assert sha(src)==sha(dst)
    rows.append(dict(path='licenses/'+name,size=dst.stat().st_size,sha256=sha(dst),classification='open-source-license'))
dst=target/worker.name
if dst.exists():
    # Only update our previously recorded host, never an unknown existing file.
    prior=json.loads((target/'veyra-rdna3-runtime-manifest.json').read_text('utf8'))
    old=next(r for r in prior['files'] if r['path']==worker.name);assert sha(dst)==old['sha256']
    dst.unlink()
shutil.copyfile(worker,dst);rows.append(dict(path=worker.name,size=dst.stat().st_size,sha256=sha(dst),classification='Veyra-open-source-host'))
shader=a.build_dir/'shaders/Rdna3NrDecode.dxil';shaderOut=a.app/'shaders'/shader.name
if shaderOut.exists():shaderOut.unlink()
shutil.copyfile(shader,shaderOut)
for filename in ('source-manifest.json',):shutil.copyfile(a.dependency_root/filename,target/filename)
out=dict(localOnly=True,releaseAuthorized=False,backend='RX7000 RedDuke, same-frame, isolated worker',sourceCommit='ab455d179922adc11f65675d399e1d553bf423f0',zludaCommit='b135e233bb7a6ff7f8690e49701f87f4944adcee',networkIdentity=identity,driverIncluded=False,mainProcessDriverReplaced=False,files=rows,guardShaderSha256=sha(shaderOut))
(target/'veyra-rdna3-runtime-manifest.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps(dict(files=len(rows),root=str(target),workerSha256=sha(dst),networkIdentity=identity),ensure_ascii=False))
