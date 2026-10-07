"""Validate completed receipts and render the final local VFG report."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007';LOGS=BASE/'logs'/TASK
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-report.py')],cwd=ROOT,check=True)
exe=sha(BASE/'build'/TASK/'veyra_qml_ui.exe');matrix=[]
for q in ('low','medium','high'):
 for m in range(2,9):
  case=read(LOGS/f'{q}{m}-nr/result.json');assert case['passed'] and case['exeSha256']==exe and case['exitCode']==0,case
  assert case['config']['nr'] and case['config']['seconds']==20 and case['observations']>=17,case
  text=(LOGS/f'{q}{m}-nr/player.log').read_text(encoding='utf8',errors='replace')
  assert not re.search(r'\[ERROR\s*\]|\[FATAL\s*\]',text),q+str(m)
  matrix.append({'quality':q,'multiplier':m,'fps':case['fps'],'status':case['status']})
rows=['| 倍率 | 源30目标fps | Low 前→后 | Medium 前→后 | High 前→后 |','| --- | ---: | ---: | ---: | ---: |']
for m in range(2,9):
 values=[]
 for q in ('low','medium','high'):
  old=read(BASE/'logs/vfg-diagnosis-20261007'/f'{q}{m}-nr/result.json');new=read(LOGS/f'{q}{m}-nr/result.json')
  suffix='（降档）' if any('降档' in s for s in new['status']) else '（未满）' if new['fps']<30*m*.99 else ''
  values.append(f'{old["fps"]:g}→{new["fps"]:g}{suffix}')
 rows.append(f'| {m}X | {30*m} | '+' | '.join(values)+' |')
rows+=['','“前”为上一轮正式2.0.5诊断参考，“后”为本轮最终候选21组。非相邻矩阵不用于精确归因百分比；下面相邻对照才用于计算收益。']
controls=[];matched=['| 对照 | 原版fps | 候选fps | 提升 | 原版CPU滚动P95中位ms | 候选CPU滚动P95中位ms |','| --- | ---: | ---: | ---: | ---: | ---: |']
for name in ('medium4','high2'):
 old=read(LOGS/f'matched-old-{name}/analysis.json');new=read(LOGS/f'matched-new-{name}/analysis.json')
 assert old['result']['passed'] and new['result']['passed'] and new['result']['exeSha256']==exe
 assert old['result']['config']==new['result']['config']
 oldfps,newfps=old['result']['fps'],new['result']['fps'];gain=(newfps/oldfps-1)*100
 a=old['timing']['medianReportedWindowP95Ms']['graphSubmitP95Ms'];b=new['timing']['medianReportedWindowP95Ms']['graphSubmitP95Ms']
 controls.append({'case':name,'oldFps':oldfps,'newFps':newfps,'fpsGainPercent':gain,'oldCpuWindowP95Ms':a,'newCpuWindowP95Ms':b,'cpuWindowP95ReductionPercent':(1-b/a)*100,'oldCadence':old['submissionCadenceMs'],'newCadence':new['submissionCadenceMs'],'oldExpired':old['timing']['cumulativeDeltas']['expiredGenerated'],'newExpired':new['timing']['cumulativeDeltas']['expiredGenerated']})
 matched.append(f'| {name} | {oldfps:g} | {newfps:g} | {gain:.1f}% | {a:.3f} | {b:.3f} |')
sync=read(LOGS/'matched-sync-medium4/result.json');assert sync['passed'] and sync['syncSubmit'] and sync['exeSha256']==exe
matched+=['',f'同一候选仅强制同步提交的控制组Medium4为{sync["fps"]:g}fps；对应正常异步组为{controls[0]["newFps"]:g}fps，支持“送显线程被SDK调用占住”的因果解释。']
native=read(LOGS/'candidate-native-720-v3.json');assert native['exit']==0
native4k=read(LOGS/'async-native-4k-v2.json');assert native4k['exit']==0
hashes=read(LOGS/'native-hash-comparison.json');assert hashes['allGpuPixelHashesIdentical'] and hashes['intermediateFrames']==168 and hashes['finalCandidateHashMatches'] and hashes['finalCandidateExeSha256']==exe
exports=read(LOGS/'export-v3-verified/summary.json');assert len(exports)==24 and all(x['exit']==(1 if x['expected']=='missing' else 0) for x in exports)
life=[dict(x,evidence='lifecycle-v3') for x in read(LOGS/'lifecycle-v3/summary.json') if x['phase'] in ('run','restore')]
life += [dict(x,evidence='lifecycle-edge-v3') for x in read(LOGS/'lifecycle-edge-v3/summary.json')]
assert {x['phase'] for x in life}=={'run','restore','missing','failure'} and len(life)==4 and all(x['passed'] for x in life)
assert 'unavailable saved backend=NVIDIA-VFG selected=AMD-FSR' in (LOGS/'lifecycle-edge-v3/missing-engine.log').read_text(encoding='utf8')
gui=read(LOGS/'lifecycle-edge-v3/export-probe.json');guiVideo=next(x for x in gui['streams'] if x['codec_type']=='video');assert guiVideo['nb_read_frames']=='64' and guiVideo['r_frame_rate']=='240/1'
settings=read(LOGS/'settings-v3.json');assert settings['exit']==0 and '331 checks, 0 failures' in (LOGS/'settings-v3.log').read_text(encoding='utf-8-sig')
validation='\n'.join(['- 最终候选构建candidate-build-v3 exit0，EXE SHA256 `'+exe+'`，核心源码ddf71b1。',
 '- 21组原片正常播放、5组相邻/同步控制均采样与退出通过；是否跑满按上表判定。',
 '- 8/10位、三质量、2X–8X、切镜/相同CUDA地址刷新/反向运动/parity与2X→4X→8X恢复：720p同步、720p异步、4K异步各913项0失败，最终EXE再做720p检查通过；168张同步/异步插帧GPU像素哈希一致。',
 '- GUI热切换质量/倍率、VFG↔DLSS6X、暂停/seek/缩放、全屏进入/返回、列表/节点预设与重启通过；缺VFG运行库沿用原有启动迁移到FSR2X并继续播放，未修改该共享策略。异步Run拒绝后实际FG关闭、原帧继续，无等待悬挂。GUI独立导出worker保持同步SDK判定，8源帧输出64帧/240fps。',
 '- VFG设置/预设/会话及倍率/PTS单元测试331项0失败。',
 '- 21组720p三质量×2X–8X实际NVENC导出及4K GTA短片原生NR+VFG4导出通过；每组源8帧、输出8×倍率、HEVC/CFR与严格递增PTS核对，无源帧丢弃。边界hold仍按原导出合同计数，不能把所有输出都称神经生成帧。取消和缺运行库导出失败清理通过。',
 '- 失败记录保留：初次两个异步拒绝断言误把QML保存的fgEnabled请求当实际后端；首次缺运行库断言误以为旧策略必定关闭FG，实际已迁移FSR；首次八帧MP4导出断言未考虑微秒time_base取整（16帧、标称60fps，平均59.999925fps）。按实际接口和时间刻度修正测试后通过，没有为迁就测试改产品。settings-tests首次漏传必需的绝对输出目录而exit2，补齐参数后331项通过。'])
path=ROOT/'docs/VFG_OPTIMIZATION_REPORT_2026-10-07.md';text=path.read_text(encoding='utf8')
text=text.replace('状态：验收进行中，以下已完成证据不代表尚未执行的检查通过。','状态：本机VFG专项短测通过，已达到部分主要档位目标；更高倍率仍有明确限制。')
assert all(marker in text for marker in ('<!-- MATRIX_TABLE -->','<!-- MATCHED_TABLE -->','<!-- VALIDATION -->'))
text=text.replace('<!-- MATRIX_TABLE -->','\n'.join(rows)).replace('<!-- MATCHED_TABLE -->','\n'.join(matched)).replace('<!-- VALIDATION -->',validation)
path.write_text(text,encoding='utf8')
receipt={'buildExeSha256':exe,'buildSource':read(LOGS/'candidate-build-v3.json')['sourceHead'],'matrix':matrix,'matched':controls,'syncControlFps':sync['fps'],'nativeHashComparison':hashes,'exportCases':len(exports),'lifecycle':life,'settings':settings,'report':str(path)}
verify=BASE/'verify'/TASK;verify.mkdir(parents=True,exist_ok=True);(verify/'measurements.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'exeSha256':exe,'matched':controls,'matrixCases':len(matrix),'exports':len(exports)},ensure_ascii=False),flush=True)
