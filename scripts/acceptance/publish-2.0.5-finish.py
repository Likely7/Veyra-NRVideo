"""Record completed publication in main without changing the release tag/tree."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='publish-2.0.5-20261007'
LOG=BASE/'logs'/TASK;OUT=BASE/'releases'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.5-control.py')],check=True)
remote=json.loads((LOG/'verify-public.json').read_text(encoding='utf8'));assert remote['passed'] and remote['latest']
merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));head=merge['mainAfter']
assert git(MAIN,'rev-parse','HEAD')==head and not git(MAIN,'status','--porcelain')
assert not (LOG/'documentation-close.json').exists()
cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'))
assets=json.loads((LOG/'assets.json').read_text(encoding='utf8'))
table='\n'.join('| '+r['name']+' | '+str(r['bytes'])+' | '+r['sha256']+' |' for r in assets)
report=f'''# Veyra 2.0.5 正式发布回执

已发布：{remote['url']}，GitHub Release ID {remote['releaseId']}，published_at {remote['publishedAt']}。正式版、非预发布，GitHub latest核对通过。发布标签v2.0.5指向main合并{head}，发布源码提交{merge['sourceCommit']}；标签/源码树相同。收尾文档另行提交，不改标签或资产。

产品沿用已测2.0.5：EXE SHA256 acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936，PE File/ProductVersion2.0.5.0。EXE/QML/shaders和NVIDIA215/AMD694项原运行组件字节保持；本轮只有发布文档、脚本和元数据。1724个开工源输入、20份当前验收收据、28旧工作树/495修改文件/53历史证据受独立guard保护。

## 实际命令和结果

Python3.11 -B运行publish-2.0.5-control.py：通过；package.py：两完整包、应用/依赖源码、5资产清单完成，逐载荷SHA和ZIP CRC通过；audit.py：逐文件manifest、厂商隔离、PE导入闭包通过；cold.py：两最终ZIP独立解压、仅系统PATH/私有profile基础播放及内嵌字幕启动通过。冷启动：{'; '.join(r['vendor']+' '+str(r['seconds'])+'秒/exit0/'+str(len(r['ownQtAndCodecModulePaths']))+'个包内Qt/FFmpeg模块' for r in cold)}。均RTX5070本机基础运行，不能代替AMD HIP推理/编码验收。

remote.py push/draft/verify-draft/publish/verify-public：普通atomic推送main+标签，草稿上传5资产，远端全部size及sha256 digest与本机一致；逐字核对正文，标签和main匹配，正式latest核验通过。实际重新下载SHA256SUMS与本机一致，5公开资产/Release页面和双QR及Ko-fi按钮HTTP可用。双QR width220、Discord/Ko-fi保留；旧v2.0.4 ID/正文/时间/资产未改。没有代发群消息或关机。

所有回执：E:/项目/Veyra/logs/{TASK}/，包括source-audit.json、stage-AMD/NVIDIA.json、cold-verify-results.json、main-merge.json、push.json、verify-draft.json、verify-public.json及remote-public-release.json。产物E:/项目/Veyra/releases/{TASK}/；源码bundle在archives/{TASK}/source-final.bundle并已verify。

## 失败和复核

开工严格guard发现新checkout13个继承文件被CRLF转换；所有内容先证实仅换行差异，再在新自有工作树恢复受保护main原字节，并确认clean-filter blob等于HEAD。旧baseline/其他工作树未改，随后guard通过。原记录checkout-line-endings.json保留。唯一历史tracked pyc保留在Git，源码ZIP明确排除；不修改/删除既有源文件。

## 正式资产

| 文件 | 字节 | SHA256 |
|---|---:|---|
{table}

依赖源码与2.0.4原包字节相同（4eccde63…），本次改名并随Release提供；patched FFmpeg、静态字幕和串流对应源完整保留。源码无proprietary SDK/runtime/模型，用户包无开发LIB/PDB/测试媒体/配置/日志。

## 数据与边界

fix2同EXE统计动画反向对照NR区间下降5.80%、UI提交下降89.06%，只限RTX5070/616.56/320Hz、GTA4K30、原版单NR内部1080、普通优先级。与2.0.3匹配差+0.009%/−0.638%。新2.0.5单轮6.295ms不拼算新提速；UI提交不等于延迟/显存。NVIDIA显存持续增长仍未定位/修复，下版排查；AMD真实高分辨率HIP离线导出/编码、物理HDR/撕裂/VRR及长期多设备稳定性未全部验收。XeSS补帧/节点离线导出仍不支持。公告同样保留这些边界。
'''
(MAIN/'docs/PUBLISH_2.0.5_REPORT_2026-10-07.md').write_text(report,encoding='utf8')
p=MAIN/'docs/WORKLOG.md';s=p.read_text(encoding='utf8');header='# Veyra 工作记录';assert s.startswith(header)
s=s.replace(header,header+'\n\n## 2026-10-07 2.0.5 正式发布完成\n\n'+
    f'已正式发布{remote["url"]}（Release ID {remote["releaseId"]}，{remote["publishedAt"]}），5资产远端大小/SHA256 digest一致，最新正式版本、对应源码/依赖源码、正文/双QR和下载可用性核对通过。两最终ZIP冷启及26个包内模块路径各通过，产品字节保持。实际命令、结果、失败复核、哈希与日志路径见[PUBLISH_2.0.5_REPORT](PUBLISH_2.0.5_REPORT_2026-10-07.md)。main合并{head}、源码{merge["sourceCommit"]}；随后只提交此正式回执，不改标签/资产/产品。NV显存未修复；公告≤1000字，只交用户，不代发不关机。\n',1);p.write_text(s,encoding='utf8')
p=MAIN/'docs/CURRENT_STATUS.md';s=p.read_text(encoding='utf8');header='# 当前项目状态 / Current Status';assert s.startswith(header)
s=s.replace(header,header+'\n\n## 2026-10-07 当前公开正式版2.0.5\n\n'+
    f'[v2.0.5]({remote["url"]})已发布并核对为latest，NVIDIA/AMD完整便携包、应用/依赖源码及SHA256SUMS共5资产的远端digest一致。产品与已测2.0.5保持，正式冷启通过，详见[发布回执](PUBLISH_2.0.5_REPORT_2026-10-07.md)。NVIDIA显存持续增长仍未定位/修复，AMD实卡离线编码仍待复测；下方各记录仅代表其当时状态。\n',1);p.write_text(s,encoding='utf8')
changed=set(git(MAIN,'diff','--name-only').splitlines())|set(git(MAIN,'ls-files','--others','--exclude-standard').splitlines())
assert changed=={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/PUBLISH_2.0.5_REPORT_2026-10-07.md'},changed
subprocess.run(['git','-C',str(MAIN),'diff','--check'],check=True)
subprocess.run(['git','-C',str(MAIN),'add','--',*sorted(changed)],check=True)
subprocess.run(['git','-C',str(MAIN),'commit','-m','docs: record verified Veyra 2.0.5 publication'],check=True)
after=git(MAIN,'rev-parse','HEAD');save(LOG/'documentation-close.json',dict(releaseMainCommit=head,mainAfter=after,files=sorted(changed)))
subprocess.run(['git','-C',str(MAIN),'push','nrvideo','main:main'],check=True)
announcement='''Veyra 2.0.5 正式版已发布！

这次重点修复稳定性与性能回退：修复OBS兼容模式滚动拖影、AMD NR黑屏合成问题和多层NR导出；AMD/Intel补帧默认优先FSR，不可用的旧DLSS配置会自动迁移。FSR视频导出不再被强制切成DLSS。自动显示同步覆盖窗口与全屏，并适配PR19/20的HDR显示调优、日志和构建修复。

本机RTX5070、GTA VI 4K输入、原版单层1080p NR、普通GPU优先级，同EXE统计动画对照中：NR处理区间降低约5.8%，UI提交减少约89%。与2.0.3同条件对照已接近持平；这些是本机限定数据，不代表所有显卡收益，也不是延迟或显存降幅。

请按显卡下载NVIDIA/AMD完整包，在新目录解压，保留旧版方便回退。NR强度5、自动/手动调控和列表/节点统一预设继续保留。

已知：NVIDIA显存持续增长尚未修复，下版深入排查；AMD实卡离线编码待复测，XeSS补帧离线导出仍不支持。反馈请附显卡、驱动、输入来源、效果设置和日志。

下载：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.5
Discord：https://discord.gg/c9aREyMj8
赞助：https://ko-fi.com/likely7
'''
assert len(announcement)<1000
(OUT/'GROUP_ANNOUNCEMENT.txt').write_text(announcement,encoding='utf8')
save(LOG/'finish.json',dict(passed=True,mainCommit=after,releaseTagCommit=head,announcementCharacters=len(announcement),announcement=str(OUT/'GROUP_ANNOUNCEMENT.txt')))
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.5-control.py')],check=True)
print('PUBLICATION DOCUMENTATION COMPLETE',after,len(announcement),'announcement characters',flush=True)
