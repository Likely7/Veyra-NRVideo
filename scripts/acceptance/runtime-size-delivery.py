"""Final local delivery index; product/source/runtime remain separate."""
import hashlib, json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
OUT=BASE/'test-packages'/TASK;LOGS=BASE/'logs'/TASK
STAGE=OUT/'Veyra-2.0.2-slim-20261004-win64-portable'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()
def read(name):return json.loads((LOGS/name).read_text(encoding='utf-8-sig'))
def write(p,r):p.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if sys.argv[1]=='index':
    archive=read('archive-audit.json');dense=read('dense-archive-audit.json');selection=read('archive-selection.json')
    primary=Path(str(STAGE)+'.7z');zipPath=Path(str(STAGE)+'.zip')
    chosen=dense if selection['selectedMethod']==dense['method'] else archive['archives'][1]
    assert digest(primary)==chosen['sha256'] and primary.stat().st_size==chosen['bytes']
    assert digest(zipPath)==archive['archives'][0]['sha256']
    clean=read('clean-smoke/summary.json');assert len(clean['results'])==2
    native=read('native-archive-final-v1.json');assert native['status']=='pass'
    assert all(read(n+'.json')['returncode']==0 for n in ('native-720-v1','native-4k-v1'))
    exports=read('export-all-v1/summary.json');assert len(exports)==21 and all(r['exit']==0 for r in exports)
    ui=read('ui-package-final/summary.json');assert len(ui)==4 and all(r['returncode']==0 for r in ui[:3])
    isolation=read('source-isolation.json');assert all(isolation.values())
    source=read('source-audit.json');bundle=read('final-bundle-audit.json')
    manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'))
    baseBytes=1151602649
    record={'schema':'veyra.local.complete-candidate.v1','localOnly':True,'releaseReady':False,'branch':'codex/runtime-size-20261004','compiledCodeCommit':manifest['baseCommit'],
            'sourceArchive':source,'gitBundle':bundle,'appSha256':digest(STAGE/'veyra_qml_ui.exe'),'primary':{**chosen,'archive':str(primary),'native7zipPassed':True},
            'zipFallback':archive['archives'][0],'comparison7z32':{**archive['archives'][1],'comparisonOnly':True,'currentBenchmarkPath':selection['comparison32'],'retained':Path(selection['comparison32']).exists()},
            'oldZipBytes':baseBytes,'primaryReductionPercent':100*(1-primary.stat().st_size/baseBytes),
            'vfgRawBefore':497894432,'vfgRawAfter':210788400,'amdRawBefore':615032656,'amdRawAfter':615032656,
            'checks':{'native720':463,'native4k':463,'settings':331,'nvencCombinations':21,'guiAndWorker':ui,'cleanPackage':clean,'nativeArchive':native,'isolation':isolation},
            'hardware':'RTX5070 / 616.56','amdInferenceVerified':False,'xboxHardwareVerified':False,'rtx40Verified':False,'hdrPrChanges':False,
            'hdrPlan':str(ROOT/'docs/HDR_DOVI_PR_REVIEW_2026-10-04.md'),'shutdown':'Requested by user; schedule only after delivery/index/source stored.'}
    write(OUT/'DELIVERY.json',record)
    mib=lambda n:f'{n/1048576:.1f}'
    text=f'''# Veyra 完整本地测试包 · 2026-10-04

首选 `{primary.name}`，{mib(primary.stat().st_size)} MiB，解压到新目录后运行 veyra_qml_ui.exe。
备用 `{zipPath.name}`，{mib(zipPath.stat().st_size)} MiB。两者相同2076载荷+manifest。
原VFG测试ZIP {mib(baseBytes)} MiB → 新7z {mib(primary.stat().st_size)} MiB，传输体积减少{record['primaryReductionPercent']:.1f}%。
VFG运行目录474.8→201.0 MiB，九个未用NPP移除；AMD必要资源586.5 MiB保持原件。
7z字典{chosen.get('dictionaryBytes',33554432)//1048576} MiB只影响解压内存，运行内存与文件内容保持。

720p/4K各463 native checks、331 settings、21真实NVENC组合、热切换/预设/重启/缺库、High8冻结worker、
新EXE极简边缘6例通过。最终独立解压所有文件SHA、官方7-Zip CRC/兼容性、包内VFG真实UI/worker、
GPU和软件各六页背景alpha/正常退出通过；原桌面/main/2077原载荷隔离复核通过。

包含Xbox音频/重连、导出码率、RTSS背景与取消NVIDIA App误检测修复、AMD NR、VFG全部档位和极简边缘修复。
仅RTX5070/616.56短测。RX9000推理、Xbox实机长稳、RTX40/616.92、HDR色度、实屏DPI/物理显示延迟/长稳未验。

HDR/DV PR13/14未施工或合并。#13修正P5 offset/完整曲线/MMR；#14先补构建依赖、改亮度统计range/array/延迟/reset。
接着解决预设版本/worker ABI，统一“输入格式—HDR处理卡—实际输出/显示校准”，参考与实机验收后按用户批准合并。
详细方案另见同目录 HDR_DOVI_PR_REVIEW_2026-10-04.md。

compiledCodeCommit: {manifest['baseCommit']}
sourceArchiveCommit: {source['sourceArchiveCommit']}（产品代码相同，后续仅报告/验收脚本）
源码ZIP `{Path(source['source']).name}`，SDK/模型/runtime不含在源码内；这是项目源码，不冒充完整第三方公开源码审计。
Git bundle `{Path(bundle['path']).name}`已verify。哈希/完整证据/源码与存档位置见DELIVERY.json及.sha256。
没有merge main、push或GitHub Release。完成保存后按用户要求安排正常关机。

7z SHA256: {chosen['sha256']}
ZIP SHA256: {archive['archives'][0]['sha256']}
APP SHA256: {record['appSha256']}
'''
    (OUT/'DELIVERY.md').write_text(text,encoding='utf8')
    print('FINAL DELIVERY INDEX PASS',primary,chosen['bytes'],record['primaryReductionPercent'])
else:raise SystemExit('index')
