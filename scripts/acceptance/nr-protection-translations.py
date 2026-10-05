"""Complete translations for the style-protection follow-up after extraction."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[2]
translations={
 '中性色保护':('中性色保護','Neutral color protection','無彩色保護'),
 '原图色彩保留':('原圖色彩保留','Source color retention','元の色を保持'),
 '亮度保持':('亮度保持','Lightness retention','明度を保持'),
 '暗部保护':('暗部保護','Shadow protection','暗部保護'),
 '自动调控力度':('自動調控力度','Automatic control amount','自動調整の強度'),
 '采用当前风格的自动参数':('採用目前風格的自動參數','Use this style’s automatic values','現在のスタイルの自動値を使う'),
 '保留原图色相；没有色相偏移时变化较小':('保留原圖色相；沒有色相偏移時變化較小','Retain source hue; little change when hue already matches','元の色相を保持。色相ずれがない場合は変化が小さくなります'),
 '软化过量色度变化；小幅变化保留':('柔化過量色度變化；小幅變化保留','Soften excessive chroma changes; retain small changes','過剰な色度変化を抑え、小さな変化は保持します'),
 '抑制白墙、云和灰色区域的额外染色':('抑制白牆、雲和灰色區域的額外染色','Suppress added tint on white walls, clouds and gray areas','白い壁、雲、灰色領域への余計な着色を抑えます'),
 '保留原图色相与饱和度；1 为完全保留色彩':('保留原圖色相與飽和度；1 為完全保留色彩','Retain source hue and saturation; 1 fully retains color','元の色相と彩度を保持。1 で色を完全に保持します'),
 '减少 NR 明暗改动；1 为保留原图亮度':('減少 NR 明暗改動；1 為保留原圖亮度','Reduce NR lightness changes; 1 retains source lightness','NR の明暗変化を減らします。1 で元の明度を保持します'),
 '减少暗部被进一步压黑，保留提亮细节':('減少暗部被進一步壓黑，保留提亮細節','Reduce further shadow darkening while retaining brighter detail','暗部のさらなる黒つぶれを抑え、明るくなる細部は保持します'),
 '只压缩向白色溢出的变化，保留高光回暗':('只壓縮向白色溢出的變化，保留高光回暗','Compress changes toward blown whites; retain highlight darkening','白飛びする変化だけを圧縮し、ハイライトの減光は保持します'),
 '同时软化过大的亮度和颜色变化':('同時柔化過大的亮度和顏色變化','Soften excessive lightness and color changes together','過大な明度と色の変化を同時に抑えます'),
 '播放时减闪；暂停单帧不体现时域效果':('播放時減閃；暫停單幀不體現時域效果','Reduce flicker during playback; a paused frame does not show temporal effects','再生時のちらつきを抑えます。一時停止の1フレームでは時間的効果は見えません'),
 '0 保留原始变化；1 按当前风格完整纠偏':('0 保留原始變化；1 按目前風格完整糾偏','0 retains raw changes; 1 applies this style’s full correction','0 は元の変化を保持、1 は現在のスタイルの補正を完全に適用'),
 '自动参数分别适配风格 0／1／2。手动可独立保留颜色、亮度和暗部；保护会降低对应变化量。时域稳定复用光流，切换方式保留手动数值。':(
  '自動參數分別適配風格 0／1／2。手動可獨立保留顏色、亮度和暗部；保護會降低對應變化量。時域穩定重用光流，切換方式保留手動數值。',
  'Automatic values are tuned for styles 0/1/2. Manual controls independently retain color, lightness and shadows, reducing the corresponding changes. Temporal stability uses optical flow; switching modes preserves manual values.',
  'スタイル 0／1／2 ごとに自動値を調整します。手動では色、明度、暗部を個別に保持し、対応する変化を抑えられます。時間的安定化はオプティカルフローを使い、モードを切り替えても手動値を保持します。')
}
path=root/'i18n/catalog.json';data=json.loads(path.read_text(encoding='utf8'));found=set()
for entry in data['entries']:
    if entry['zh'] in translations:
        entry.update(zip(('zh-TW','en','ja'),translations[entry['zh']]))
        found.add(entry['zh'])
assert found==set(translations),set(translations)-found
path.write_text(json.dumps(data,ensure_ascii=False,indent=1)+'\n',encoding='utf8')
print('NR style translations complete',len(found))
