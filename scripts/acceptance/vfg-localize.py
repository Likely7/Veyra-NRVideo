"""Translations for the VFG controls/status and newly collected field-repair status."""
import json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
translations={
'NVIDIA VFG 未能启用（需要 Ada / Blackwell 显卡、VFG 运行组件和支持的输出格式），已关闭补帧；原因见日志':('NVIDIA VFG could not start (requires an Ada / Blackwell GPU, VFG runtime and a supported output format). Frame generation is off; see the log.','NVIDIA VFG を有効にできませんでした（Ada / Blackwell GPU、VFG ランタイム、対応する出力形式が必要）。フレーム生成はオフです。ログをご確認ください。'),
'VFG 支持 2X～8X 和低 / 中 / 高质量；6X～8X 为实验档，高倍高质量会增加处理时间':('VFG supports 2X–8X and Low / Medium / High quality. 6X–8X are experimental; higher multipliers and quality take more processing time.','VFG は 2X～8X と低 / 中 / 高品質に対応します。6X～8X は実験設定です。倍率や品質を上げると処理時間が増えます。'),
'VFG 质量':('VFG quality','VFG 品質'),
'VFG 质量设置未被接受，原设置保留':('VFG quality was not accepted; the previous setting was kept.','VFG 品質設定が拒否されたため、前の設定を保持しました。'),
'VFG 需要 NVIDIA Ada / Blackwell 显卡（RTX 40 / 50）':('VFG requires an NVIDIA Ada / Blackwell GPU (RTX 40 / 50).','VFG には NVIDIA Ada / Blackwell GPU（RTX 40 / 50）が必要です。'),
'Xbox 串流中断（{}），正在重连 {}/3…':('Xbox stream interrupted ({}); reconnecting {}/3…','Xbox ストリームが中断されました（{}）。再接続中 {}/3…'),
'Xbox 已重新连接':('Xbox reconnected','Xbox に再接続しました'),
'Xbox 服务结束了串流（':('The Xbox service ended the stream (','Xbox サービスがストリームを終了しました（'),
'Xbox 重连失败：':('Xbox reconnection failed: ','Xbox の再接続に失敗：'),
'Xbox 重连时 GPU 排空失败':('Could not drain the GPU queue during Xbox reconnection.','Xbox 再接続時に GPU キューの待機処理に失敗しました。'),
'低':('Low','低'),
'所选补帧后端的倍率已调整为 %1X':('The selected frame-generation backend adjusted the multiplier to %1X.','選択したフレーム生成方式の倍率を %1X に調整しました。'),
'未找到 VFG 运行组件，请在组件页核对路径':('VFG runtime components were not found. Check their paths on the Components page.','VFG ランタイムが見つかりません。コンポーネントページでパスをご確認ください。'),
'预设库 VFG 质量字段损坏':('The preset library has an invalid VFG quality field.','プリセットライブラリの VFG 品質フィールドが破損しています。'),
'预设库补帧后端无效':('The preset library has an invalid frame-generation backend.','プリセットライブラリのフレーム生成方式が無効です。'),
'高':('High','高'),
'高质量需要更多处理时间':('High quality takes more processing time.','高品質にはより多くの処理時間が必要です。')}
path=ROOT/'i18n/catalog.json';data=json.loads(path.read_text(encoding='utf8'))
for e in data['entries']:
    if e['zh'] in translations:e['en'],e['ja']=translations[e['zh']]
path.write_text(json.dumps(data,ensure_ascii=False,indent=1)+'\n',encoding='utf8')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/i18n/to_traditional.py')],check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/i18n/extract.py'),'--check'],check=True)
