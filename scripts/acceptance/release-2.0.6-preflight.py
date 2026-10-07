"""Require actual fresh evidence, document it and merge the reviewed release."""
from pathlib import Path
import ast,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
LOG=BASE/'logs'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def load(p):return json.loads((LOG/p).read_text(encoding='utf8'))
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')];subprocess.run(guard,check=True)
assert not (LOG/'main-merge.json').exists()
build=load('build-product.json');assert build['exit']==0
for n,h in build['sourceFiles'].items():assert sha(ROOT/n)==h,n
for name in ('amd-graph','amd-abi','effect-chain','units','gpu'):
 rows=load(name+'/summary.json');assert rows and all(r['passed'] and r['exit']==0 for r in rows),name
graphtext=(LOG/'amd-graph/veyra_amd_nr_graph_tests.log').read_text(encoding='utf8')
assert '20 cases, 0 failures' in graphtext and 'D3D12_DEBUG errors=0' in graphtext
assert '62 checks 0 failures' in (LOG/'amd-abi/veyra_lmxxf_nr_tests.log').read_text(encoding='utf8')
assert '913 checks 0 failures' in (LOG/'gpu/veyra_vfg_gpu_tests.log').read_text(encoding='utf8')
assert '331 checks, 0 failures' in (LOG/'units/veyra_vfg_settings_tests.log').read_text(encoding='utf8')
colors=(LOG/'gpu/veyra_hdr_color_tests.log').read_text(encoding='utf8');colorcount=len(re.findall(r'\bpass=1\b',colors));assert colorcount>=124 and not re.search(r'\bpass=0\b',colors)
perf=[load(n+'/result.json') for n in ('gta-medium4','gta-high2')]
for r,target in zip(perf,(120,60)):
 assert r['passed'] and not r['errors'] and r['fps']>=target*.97 and r['observations']>=17,r
 assert r['exeSha256']==sha(BASE/'build'/TASK/'veyra_qml_ui.exe')
 assert any('requested=2 actual=2' in line for line in r['normalSchedulingClass'])
exports=load('exports/summary.json');assert len(exports)==4 and all(r['exit']==(1 if r['expected']=='missing' else 0) for r in exports)
assert exports[0]['frames']==exports[1]['frames']==32
qr='5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd';assert sha(ROOT/'docs/images/2.0.6/community-group.png')==qr
for name in ('README.md','README_CN.md','docs/RELEASE_SUPPORT.md'):
 t=(ROOT/name).read_text(encoding='utf8');assert t.count('width="220"')==2 and 'https://discord.gg/c9aREyMj8' in t and 'https://ko-fi.com/likely7' in t
 assert '/v1.4.0/docs/images/1.4.0/donate-wechat.jpg' in t and '/v2.0.6/docs/images/2.0.6/community-group.png' in t
 assert ('community group 5' if name=='README.md' else '交流群 5') in t
for p in (ROOT/'scripts/acceptance').glob('release-2.0.6-*.py'):ast.parse(p.read_text(encoding='utf8'),filename=str(p))
testrows='\n'.join('| '+r['target']+' | '+str(round(r['seconds'],3))+' | exit0 / PASS |' for name in ('amd-graph','amd-abi','effect-chain','units','gpu') for r in load(name+'/summary.json'))
perftable='\n'.join('| '+name+' | '+str(r['fps'])+' | '+str(r['observations'])+' | '+str(round(r['wallSeconds'],3))+' |' for name,r in zip(('Medium4X','High2X'),perf))
report=f'''# Veyra 2.0.6 新构建发布验收

产品源码起点bf6e651，包含VFG、Blackmagic和NR→FSR已完成修复；本轮生产改动仅CMake数字/显示版本2.0.6，其余为发布文档/脚本/用户授权的群码。全新构建使用正式2.0.5外置依赖cache，build-product.py的sourceFiles逐hash绑定最终源码（{len(build['sourceFiles'])}个输入），不以历史构建冒充本次通过。

## 本轮实际运行

`python -B scripts/acceptance/release-2.0.6-build.py build-product veyra_qml_ui veyra_amd_nr_graph_tests veyra_lmxxf_nr_tests veyra_effect_chain_tests veyra_vfg_gpu_tests veyra_vfg_settings_tests veyra_vfg_export_probe veyra_capture_color_tests veyra_capture_format_selection_tests veyra_hdr_color_tests`：exit0，{build['seconds']:.3f}秒。EXE SHA256 {sha(BASE/'build'/TASK/'veyra_qml_ui.exe')}；版本资源由打包独立PE核对2.0.6.0。编译保留既有C4244转换警告，没有构建错误。

`release-2.0.6-amd-tests.py amd-graph after / amd-abi abi / effect-chain units`，`release-2.0.6-tests.py prepare / units / gpu`依次执行，无同时GPU工作，私有E盘profile/TEMP。NR图20用例0失败、10个marker全通过/最大码值偏差0/D3D12错误0；ABI62检查、效果链264条PASS；采集颜色213条PASS、格式选择0失败；GPU颜色{colorcount}项pass=1含HDYC/翻转709；VFG913检查/settings331检查均0失败。

| 程序 | 秒 | 结果 |
|---|---:|---|
{testrows}

`release-2.0.6-preview.py case gta-medium4 --multiplier 4 --quality 1 --no-verbose`及`case gta-high2 --multiplier 2 --quality 2 --no-verbose`：新2.0.6 EXE、用户GTA4K30视频、单层原版内部1080p NR、5秒预热+20秒观察；实际GPU请求/生效class2（普通）、呈现队列priority0；无竞争负载。均PASS/无错误日志、正常状态，读数为软件提交统计：

| 本轮复测 | FPS中位数 | 1秒采样数 | 进程总秒 |
|---|---:|---:|---:|
{perftable}

本轮只做新版本正常运行确认，没有新跑修改前二进制。发布说明中的+50.0%/+69.0%来自[VFG优化报告](VFG_OPTIMIZATION_REPORT_2026-10-07.md)相邻匹配实验（80→120/35.5→60），不是用本轮单次值拼算百分比；相同核心源码hash可追溯。不能推断所有显卡、屏幕延迟或显存收益。

`release-2.0.6-exports.py exports`：真实NVENC HEVC导出720p30中等4X、GTA4K30+原版1080p NR中等4X，均8源帧→32编码帧、120fps/CFR/严格递增PTS、时长约8/30秒；4K尺寸3840×2160。另取消和缺失运行库失败清理通过（缺失运行库exit1是预期）。只读沿用已保存8帧GTA fixture，不修改用户原视频。四项输出/ffprobe/PTS详见logs/exports/summary.json。

## 发布阶段的后置门槛

干净源码提交并无快进合并main后，package.py生成两厂商完整包及应用/依赖源码/校验共5资产，逐载荷hash/ZIP CRC/PE依赖/vendor边界必须通过；cold.py只从最终ZIP独立解压、仅系统PATH/私有profile运行基本播放并核对包内Qt/FFmpeg模块。remote.py必须在全部通过后才允许草稿→正式发布。最终阶段结果以E:/项目/Veyra/logs/{TASK}/的source-audit.json、stage-AMD/NVIDIA.json、cold-verify-results.json、verify-draft.json、verify-public.json和发布后main回执为准；此文档在打包前固定，不伪造尚未发生的冷启/远端结果。

## 边界、审查与保护

NR graph使用RTX5070+真实FSR3.1.5+确定性测试NR provider，验证结果传递，不能代替AMD HIP/FSR4.1/Xbox实卡。测试provider只放tests，不随包发布。Blackmagic无本机硬件；2.0.6未重跑此前USB真卡连接验收，也不宣称真实HDMI已恢复。NVIDIA显存持续增长未修复，Xbox独立硬解回退不在范围内。

本轮自动产品测试未发生失败；脚本检索阶段曾读取不存在的旧脚本文件名，改用实际vfg-opt-export.py，不影响产品验证。发布审查修正README英文二维码alt遗留group4为group5。既有编译警告与测试provider故障注入的ERROR均有原始日志，不把ABI预期错误误报为真实AMD推理失败。其他阶段的意外失败必须另记，不覆盖原始回执。

开工baseline SHA256 ee8aab7b3484eabc90d55b59547dd5bd7934b14385ebff791c6f11b2eab3aba4；guard保护33个其他工作树/495个已有修改/3678旧文件。newQR原字节SHA256 {qr}，双QR220、原赞助/Discord/Ko-fi保留。无SDK/模型新增源码Git，无修改旧产物/用户profile/驱动/其他FG，无代发群消息/关机。

全套产物在E:/项目/Veyra/build、tests、logs、tmp、releases、verify、archives/{TASK}对应目录。实际命令、结果、日志和来源以本计划/本验收及各JSON为证，不宣称独立Reviewer或未执行硬件验收。
'''
(ROOT/'docs/RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md').write_text(report,encoding='utf8')
p=ROOT/'docs/WORKLOG.md';s=p.read_text(encoding='utf8');header='# Veyra 工作记录';assert s.startswith(header)
s=s.replace(header,header+f'\n\n## 2026-10-07 2.0.6 正式发布施工\n\n用户明确授权构建、合并main、GitHub正式发布及换5群二维码。隔离codex/release-2.0.6-20261007起点bf6e651，main/nrvideo起点af5bfc3；VFG/Blackmagic/AMD NR→FSR修复保持原产品字节，CMake仅版本改2.0.6。全新构建{build["seconds"]:.3f}秒exit0，图20/ABI62/链264/采集213/颜色{colorcount}/VFG913+设置331及四导出检查通过；GTA普通优先级中等4X{perf[0]["fps"]}FPS/高2X{perf[1]["fps"]}FPS。详细命令、数据、失败复核和未测硬件见[本轮验收](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md)、[发布计划](RELEASE_2.0.6_PLAN_2026-10-07.md)。新群码原字节/有效期10月14日前，原赞助/Discord/Ko-fi保留。所有产物E:/项目/Veyra/对应{TASK}目录。33旧工作树/495修改/3678证据由独立guard保护。后续打包/最终ZIP冷启/远端发布以实际回执追加，NVIDIA显存增长未修复。\n',1);p.write_text(s,encoding='utf8')
subprocess.run(guard,check=True);subprocess.run(['git','-C',str(ROOT),'diff','--check'],check=True)
assert git(MAIN,'rev-parse','HEAD')=='af5bfc3a66afce3047868229a3070a91c0fc9c22' and not git(MAIN,'status','--porcelain')
assert git(ROOT,'ls-remote','--heads','nrvideo','refs/heads/main').split()[0]==git(MAIN,'rev-parse','HEAD')
subprocess.run(['git','-C',str(ROOT),'add','--','AGENTS.md','CMakeLists.txt','README.md','README_CN.md','docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_SUPPORT.md','docs/images/2.0.6/community-group.png','docs/BUILD_2.0.6.md','docs/RELEASE_NOTES_2.0.6.md','docs/RELEASE_2.0.6_PLAN_2026-10-07.md','docs/RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md',*[str(p.relative_to(ROOT)).replace('\\','/') for p in sorted((ROOT/'scripts/acceptance').glob('release-2.0.6-*.py'))]],check=True)
subprocess.run(['git','-C',str(ROOT),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(ROOT),'commit','-m','Release Veyra 2.0.6 with VFG, NR/FSR and capture fixes'],check=True)
source=git(ROOT,'rev-parse','HEAD');assert not git(ROOT,'status','--porcelain')
before=git(MAIN,'rev-parse','HEAD')
subprocess.run(['git','-C',str(MAIN),'merge','--no-ff',source,'-m','Merge validated Veyra 2.0.6 release'],check=True)
after=git(MAIN,'rev-parse','HEAD');assert git(MAIN,'rev-parse','HEAD^{tree}')==git(ROOT,'rev-parse','HEAD^{tree}')
(LOG/'main-merge.json').write_text(json.dumps(dict(mainBefore=before,mainAfter=after,sourceCommit=source),indent=2),encoding='utf8')
subprocess.run(guard,check=True);print('REVIEWED RELEASE SOURCE AND MAIN',source,after,flush=True)
