"""Read-only review checks and numeric counterexamples; no contributor code runs."""
import json
import math
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=Path('E:/项目/Veyra/logs/minimal-edge-hdr-review-20261004')
HEAD13='f08934b8a535bb0ac085cd8f5689b9029b416726'
HEAD14='57f2541b3173b5285661ddf6c448ffb2722f6433'
def show(head,path):return subprocess.check_output(['git','show',head+':'+path],cwd=ROOT).decode('utf-8-sig')
def files(head):return set(subprocess.check_output(['git','ls-tree','--name-only','-r',head],cwd=ROOT).decode().splitlines())

tree14=files(HEAD14)
graph14=show(HEAD14,'src/pipeline/EnhanceGraph.cpp')
shader14=show(HEAD14,'shaders/YuvToLinearRgb.hlsl')
dependency='include/veyra/pipeline/DolbyVisionP5.h'
assert '#include "veyra/pipeline/DolbyVisionP5.h"' in graph14 and dependency not in tree14
assert 'packDolbyVisionP5Constants(resolved.dolbyVisionP5' in graph14
assert 'DoviP5ToPqBt2020' not in shader14
assert 'ycc_to_rgb_offset' not in show(HEAD13,'include/veyra/source/DolbyVisionRpu.h')
assert '0.5' in show(HEAD13,'shaders/YuvToLinearRgb.hlsl')
assert 'Texture2D<float> sourceLuma' in show(HEAD14,'shaders/HdrSceneReduce.hlsl')
assert 'reduceSrv.ViewDimension=isArray?D3D12_SRV_DIMENSION_TEXTURE2DARRAY' in graph14

# Independent ST2084 formulas, applied to neutral limited-range P010 values.
# This demonstrates range error; it does not validate the proposed tone mapper.
m1,m2,c1,c2,c3=2610/16384,2523/32,3424/4096,2413/128,2392/128
def pq(nits):
    y=(nits/10000)**m1
    return ((c1+c2*y)/(1+c3*y))**m2
def nits(code):
    p=max(code,0)**(1/m2)
    return 10000*(max(p-c1,0)/max(c2-c3*p,1e-6))**(1/m1)
rows=[]
for intended in (0,100,1000,4000,10000):
    code=round(64+876*pq(intended))
    proxy=code//4
    rows.append({'intendedNits':intended,'p010Code':code,'eightBitProxy':proxy,
                 'rawProxyAsPQ_Nits':nits(proxy/255),'rangeNormalizedNits':nits((code-64)/876)})

# A highlight and a black background sharing a reduce cell get averaged BEFORE
# histogramming. Percentiles of these averages cannot retain their pixel peak.
cell=nits((pq(1000)+63*pq(0))/64)
report={'pr13':HEAD13,'pr14':HEAD14,'executedContributorCode':False,
        'pr14MissingTrackedDependency':dependency,'pr14P5ShaderMissing':True,
        'pr13RpuOffsetsIgnored':True,'pr14ArraySrvAgainstTexture2D':True,
        'limitedP010NeutralExamples':rows,'one1000NitPixelIn64PqSamples_AverageAsNits':cell,
        'qualification':'Static source checks and independent arithmetic, not PR build/GPU acceptance.'}
OUT.mkdir(parents=True,exist_ok=True)
path=OUT/'hdr-pr-static-findings.json'
path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
