"""Add only this repair's user-visible translations, preserving the catalog."""
import json
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'i18n/catalog.json'
data=json.loads(p.read_text(encoding='utf8'))
translations=[
    ('应用','套用','Apply','適用','PlaybackRateDialog.qml'),
    ('自定义…','自訂…','Custom…','カスタム…','PlaybackRateButton.qml'),
    ('自定义播放速度','自訂播放速度','Custom playback speed','カスタム再生速度','PlaybackRateDialog.qml'),
    ('播放倍速','播放倍速','Playback speed','再生速度','PlaybackRateDialog.qml'),
    ('范围 0.25～4.00 倍，保持原有音调','範圍 0.25～4.00 倍，保持原有音調','Range 0.25–4.00×, preserving pitch','範囲 0.25～4.00 倍、音程を維持','PlaybackRateDialog.qml'),
    ('请输入 0.25～4.00 之间的倍速','請輸入 0.25～4.00 之間的倍速','Enter a speed between 0.25 and 4.00','0.25～4.00 の速度を入力してください','PlaybackRateDialog.qml'),
    ('当前播放状态无法修改倍速','目前播放狀態無法修改倍速','Playback speed cannot be changed in the current state','現在の再生状態では速度を変更できません','PlaybackRateDialog.qml'),
]
for zh,tw,en,ja,source in translations:
    old=next((e for e in data['entries'] if e['zh']==zh and not e.get('ctx')),None)
    if old is None:
        data['entries'].append(dict(zh=zh,src='qml/Veyra/'+source,**{'zh-TW':tw,'en':en,'ja':ja}))
p.write_text(json.dumps(data,ensure_ascii=False,indent=1)+'\n',encoding='utf8')
print('Custom rate translations:',len(translations))
