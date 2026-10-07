"""Close the verified publication with a documentation-only main commit."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
LOG=BASE/'logs'/TASK;OUT=BASE/'releases'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')];subprocess.run(guard,check=True)
remote=json.loads((LOG/'verify-public.json').read_text(encoding='utf8'));assert remote['passed'] and remote['latest']
merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));head=merge['mainAfter']
assert git(MAIN,'rev-parse','HEAD')==head and not git(MAIN,'status','--porcelain')
assert not (LOG/'documentation-close.json').exists()
cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'));assets=json.loads((LOG/'assets.json').read_text(encoding='utf8'))
table='\n'.join('| '+r['name']+' | '+str(r['bytes'])+' | '+r['sha256']+' |' for r in assets)
report=f'''# Veyra 2.0.6 正式发布回执

已发布：{remote['url']}；Release ID {remote['releaseId']}，published_at {remote['publishedAt']}，非预发布，latest核对通过。标签v2.0.6指向main合并{head}，发布源码{merge['sourceCommit']}，二者树完全相同。收尾文档另行提交，不改标签、产品或资产。

## 构建与回归

按[发布验收](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md)执行全新2.0.6产品/测试构建、NR→FSR图20项、ABI62项、效果链264项、采集颜色213项/格式协商、GPU颜色124项、VFG GPU913项/settings331项以及四项NVENC导出（720p4X、4K NR4X、取消、缺失运行库）和GTA中等4X/高2X普通优先级预览。构建源输入逐hash绑定最终源码；PE数字版本2.0.6.0。真实AMD HIP/FSR4.1/Xbox及Blackmagic HDMI未在本机实卡验收，NVIDIA持续显存增长未修复。

package.py对干净发布源码生成完整包和对应源码；audit.py对两厂商所有manifest文件、运行组件身份、驱动排除及PE依赖闭包通过；ZIP CRC和全部载荷SHA通过。最终ZIP冷启：{'; '.join(r['vendor']+' '+str(r['seconds'])+'秒/exit0/'+str(len(r['ownQtAndCodecModulePaths']))+'个包内Qt/FFmpeg模块' for r in cold)}。均为RTX5070本机基础播放，不能代替AMD硬件推理/编码。

remote.py push/draft/verify-draft/publish/verify-public：普通atomic推送main+标签，草稿上传5资产，服务器大小/digest逐项等于本机；正文逐字一致、标签/main一致，正式latest核对通过。重新下载SHA256SUMS字节一致，5资产/页面/双QR/Ko-fi按钮HTTP200。新群5二维码远端SHA256等于用户原图5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd，双QR各width220，原赞助URL、Discord和Ko-fi保留。旧v2.0.5 ID/正文/发布时间/资产完整保留。

## 资产

| 文件 | 字节 | SHA256 |
|---|---:|---|
{table}

依赖源码与2.0.5逐字节相同4eccde63…，仅改名2.0.6；patched FFmpeg、静态依赖源码及重链接材料保留。应用源码ZIP来自干净发布提交，仅排除一份历史tracked pyc；Git内原文件保留。源码无SDK/runtime/模型，用户包无测试provider/日志/媒体/个人profile/PDB/LIB。

## 保护与路径

执行回执：E:/项目/Veyra/logs/{TASK}/；最终资产：E:/项目/Veyra/releases/{TASK}/；独立解压：E:/项目/Veyra/verify/{TASK}/；构建/测试/tmp分别在同任务子目录。archives/{TASK}/start.json固定开工字节，source-before/final.bundle可恢复源码。guard复核33个其他工作树、495个已有修改及3678项旧产物/证据；main只允许本次合并与三文件收尾提交，不修改旧baseline。实际失败及修正见发布验收。群公告写入GROUP_ANNOUNCEMENT.txt，只交用户，不代发、不关机。

## 数据边界

VFG +50.0%/+69.0%来自此前相邻匹配的RTX5070/GTA4K30/单层1080p NR/普通优先级对照；新2.0.6单轮复测单独记录，不拼成新因果百分比。计量是软件呈现提交FPS，不等于物理刷新/屏幕延迟/显存降幅。AMD图回归使用真实FSR3.1.5和确定性NR测试provider，不能冒充HIP推理。
'''
(MAIN/'docs/RELEASE_2.0.6_REPORT_2026-10-07.md').write_text(report,encoding='utf8')
for name,header,entry in [
 ('WORKLOG.md','# Veyra 工作记录',f'## 2026-10-07 2.0.6 正式发布完成\n\n[GitHub Release]({remote["url"]})已发布（ID {remote["releaseId"]}，{remote["publishedAt"]}），5资产服务器SHA256/大小一致，最新正式版、对应源码、双QR和公开下载核对通过。新群5二维码原字节，赞助/Discord/Ko-fi保留。完整命令、结果、资产哈希、边界及路径见[发布回执](RELEASE_2.0.6_REPORT_2026-10-07.md)。标签{head}、源码{merge["sourceCommit"]}；随后只追加三文件收尾文档。NV显存持续增长未修复。'),
 ('CURRENT_STATUS.md','# 当前项目状态 / Current Status',f'## 2026-10-07 当前公开正式版2.0.6\n\n[v2.0.6]({remote["url"]})已发布且核对latest。VFG调度、AMD NR→FSR输出、Blackmagic兼容整合；NVIDIA/AMD完整包、应用/依赖源码及SHA256SUMS共5资产远端digest一致，最终ZIP冷启通过。详见[发布回执](RELEASE_2.0.6_REPORT_2026-10-07.md)。5群二维码已替换，图片标注10月14日前有效。NV显存持续增长未修复，AMD/Blackmagic实卡复测边界见验收；下方记录仅代表当时状态。')]:
 p=MAIN/'docs'/name;s=p.read_text(encoding='utf8');assert s.startswith(header);p.write_text(s.replace(header,header+'\n\n'+entry+'\n',1),encoding='utf8')
changed=set(git(MAIN,'diff','--name-only').splitlines())|set(git(MAIN,'ls-files','--others','--exclude-standard').splitlines())
assert changed=={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_2.0.6_REPORT_2026-10-07.md'},changed
subprocess.run(['git','-C',str(MAIN),'diff','--check'],check=True)
subprocess.run(['git','-C',str(MAIN),'add','--',*sorted(changed)],check=True)
subprocess.run(['git','-C',str(MAIN),'commit','-m','docs: record verified Veyra 2.0.6 publication'],check=True)
after=git(MAIN,'rev-parse','HEAD');save(LOG/'documentation-close.json',dict(releaseMainCommit=head,mainAfter=after,files=sorted(changed)))
subprocess.run(guard,check=True)
subprocess.run(['git','-C',str(MAIN),'push','nrvideo','main:main'],check=True)
announcement='''Veyra 2.0.6 正式版已发布！

这次重点优化VFG补帧，并修复AMD NR结果未进入后续FSR处理的问题，增加Blackmagic/DeckLink采集兼容。

VFG不再让SDK提交工作长时间占住呈现线程。本机RTX5070、GTA VI 4K30输入、原版单层1080p NR、普通GPU优先级，同条件对照：中等4X从80到120 FPS，提升50%；高档2X从35.5到60 FPS，提升约69%。这是软件提交帧率的限定实测，不代表所有显卡收益，高倍率仍可能受GPU预算限制。

AMD方面，修复NR明明执行了，FSR/缩放却重新读取原图，导致开关NR画面没变化的问题。Blackmagic方面补齐WDM连接、HDYC颜色格式和驱动属性入口；AMD实卡及Blackmagic真实输入欢迎继续反馈。

按显卡下载NVIDIA或AMD完整包，在新目录解压，保留旧版方便回退。已知NVIDIA显存持续增长仍未解决，本次不宣称修复。

4群已满，已换5群二维码，图片标注10月14日前有效。原微信赞助、Discord和Ko-fi保留。反馈请附显卡、驱动、输入来源、效果设置及日志。

下载：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.6
Discord：https://discord.gg/c9aREyMj8
赞助：https://ko-fi.com/likely7
'''
assert len(announcement)<1000;(OUT/'GROUP_ANNOUNCEMENT.txt').write_text(announcement,encoding='utf8')
save(LOG/'finish.json',dict(passed=True,mainCommit=after,releaseTagCommit=head,announcementCharacters=len(announcement),announcement=str(OUT/'GROUP_ANNOUNCEMENT.txt')))
subprocess.run(guard,check=True);print('PUBLICATION DOCUMENTATION COMPLETE',after,len(announcement),'characters',flush=True)
