"""Fill the zh-TW column of i18n/catalog.json from the Simplified source.

usage: python -B scripts/i18n/to_traditional.py [--all]

Taiwan wording first (视频 -> 影片, 显卡 -> 顯示卡, 默认 -> 預設 ...), then Windows' own
Simplified -> Traditional character mapping (LCMapStringEx, LCMAP_TRADITIONAL_CHINESE).
Only empty zh-TW cells are filled unless --all is given, so hand corrections stay.
"""
import ctypes
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / 'i18n/catalog.json'

# Simplified phrase -> Taiwan phrase (already Traditional). Longest first when applied.
TERMS = {
    '超分辨率': '超解析度', '超分': '超解析', '分辨率': '解析度',
    '屏幕捕获': '螢幕擷取', '全屏': '全螢幕', '屏幕': '螢幕', '录屏软件': '螢幕錄影軟體', '录屏': '螢幕錄影',
    '采集卡': '擷取卡', '采集': '擷取', '捕获': '擷取',
    '视频': '影片', '音频': '音訊', '显卡': '顯示卡', '显存': '顯示記憶體', '内存': '記憶體',
    '默认': '預設', '设置': '設定', '文件夹': '資料夾', '文件名': '檔名', '文件': '檔案',
    '软件': '軟體', '硬件': '硬體', '网络': '網路', '局域网': '區域網路', '以太网': '乙太網路',
    '鼠标': '滑鼠', '键盘': '鍵盤', '快捷键': '快速鍵', '手柄': '手把', '触摸板': '觸控板',
    '质量': '品質', '画质': '畫質', '信息': '資訊', '加载': '載入', '保存': '儲存', '打开': '開啟',
    '支持': '支援', '运行库': '執行庫', '运行': '執行', '优化': '最佳化', '界面': '介面', '插件': '外掛',
    '驱动程序': '驅動程式', '服务器': '伺服器', '账号': '帳號', '退出登录': '登出', '登录': '登入',
    '刷新率': '更新率', '程序': '程式', '数据': '資料', '创建': '建立', '设备': '裝置', '用户': '使用者',
    '选项': '選項', '模板': '範本', '缓存': '快取', '线程': '執行緒', '进程': '處理程序', '图标': '圖示',
    '文本': '文字', '剪贴板': '剪貼簿', '粘贴': '貼上', '实时': '即時', '激活': '啟用', '禁用': '停用',
    '卸载': '解除安裝', '导出': '匯出', '导入': '匯入', '菜单': '選單', '窗口': '視窗', '链接': '連結',
    '字体': '字型', '光标': '游標', '码率': '位元率', '比特率': '位元率', '队列': '佇列', '拖动': '拖曳',
    '滚动': '捲動', '全局': '全域', '内置': '內建', '自带': '內建', '着色器': '著色器', '硬件加速': '硬體加速',
    '配置': '設定', '宽带': '寬頻', '回车': 'Enter',
    # One-to-many characters Windows' mapping leaves alone or gets wrong.
    '复制': '複製', '重复': '重複', '复杂': '複雜', '回复': '回覆', '合并': '合併', '日志': '日誌',
    '标志': '標誌', '游戏': '遊戲', '游玩': '遊玩', '关系': '關係', '联系': '聯繫', '标签': '標籤',
    '校准': '校準', '为准': '為準', '采样': '取樣', '项目': '專案', '耗尽': '耗盡', '发布': '發佈', '指针': '指標', '兼容': '相容',
}
# After the mapping: characters it leaves Simplified, and its 臺 where Taiwan writes 台.
AFTER = {'后': '後', '里': '裡', '并': '並', '于': '於', '范': '範', '余': '餘', '么': '麼', '几': '幾',
         '冲': '衝', '沖': '衝', '松': '鬆', '臺': '台'}

LCMAP_TRADITIONAL_CHINESE = 0x04000000
_kernel = ctypes.windll.kernel32


def to_traditional(text):
    for simplified in sorted(TERMS, key=len, reverse=True):
        text = text.replace(simplified, TERMS[simplified])
    size = _kernel.LCMapStringEx('zh-CN', LCMAP_TRADITIONAL_CHINESE, text, len(text), None, 0, None, None, None)
    buf = ctypes.create_unicode_buffer(size + 1)
    _kernel.LCMapStringEx('zh-CN', LCMAP_TRADITIONAL_CHINESE, text, len(text), buf, size, None, None, None)
    out = buf.value[:size]
    return ''.join(AFTER.get(ch, ch) for ch in out)


def main():
    data = json.loads(CATALOG.read_text(encoding='utf-8'))
    filled = 0
    for e in data['entries']:
        if e.get('zh-TW') and '--all' not in sys.argv:
            continue
        e['zh-TW'] = to_traditional(e['zh'])
        filled += 1
    CATALOG.write_text(json.dumps(data, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print('zh-TW filled', filled)


if __name__ == '__main__':
    main()
