import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: guide
    objectName: "capture-guide-dialog"
    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(760, Math.max(1, parent ? parent.width - 32 : 640))
    height: Math.min(680, Math.max(1, parent ? parent.height - 32 : 480))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        reader.contentY = 0
        reader.forceActiveFocus()
    }

    background: Rectangle {
        readonly property bool videoCover: guide.visible
        readonly property real coverRadius: 14
        color: Theme.bgOuter
        radius: coverRadius
        border.color: Theme.stroke
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("采集格式与补帧指南")
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH2
                font.bold: true
                wrapMode: Text.Wrap
            }
            VButton {
                objectName: "capture-guide-close"
                text: qsTr("关闭")
                ghost: true
                onClicked: guide.close()
            }
        }

        Flickable {
            id: reader
            objectName: "capture-guide-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: article.height + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            ScrollBar.vertical: VScrollBar { wide: true }

            function scrollBy(delta) {
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + delta))
            }
            Keys.onUpPressed: scrollBy(-38)
            Keys.onDownPressed: scrollBy(38)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    scrollBy((event.key === Qt.Key_PageUp ? -1 : 1) * height * 0.85)
                    event.accepted = true
                }
            }

            Text {
                id: article
                objectName: "capture-guide-article"
                width: Math.max(1, reader.width - 14)
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: 14
                text: guide.visible ? [
                    "## " + qsTr("先选对采集格式"),
                    qsTr("普通 SDR 游戏，我会先试 **1440p60 · NV12 · SDR · 709**。1080p 屏就从 1080p60 开始。先确认基础画面和帧率稳定，再开补帧和超分。"),
                    qsTr("**采集原生 HDR10 时，P010 要配 HDR · PQ。** 主机开启 HDR，采集页「像素格式」选 **P010**，「输入色彩（SDR / HDR）」选 **HDR · PQ**，然后重新连接。**NV12、YUY2 和 RGB 在这套指南里都用 SDR · 709。**"),
                    [
                        "- **NV12** · " + qsTr("8 位 4:2:0。带宽省，跑高分辨率和高帧率时我优先用它。采集卡支持 4K60 NV12，也可以直接用。"),
                        "- **YUY2** · " + qsTr("8 位 4:2:2，保留更多纵向色彩细节。卡上没有合适的 NV12 档位，或细小彩色文字确实需要 4:2:2 时，可以试它。"),
                        "- **RGB** · " + qsTr("常见的是 RGB24，带宽更高。确实需要 RGB 或设备在该档只有 RGB 时再选。若自动去重一直报 60 帧，可以用 NV12 对照一下。"),
                        "- **P010** · " + qsTr("10 位 4:2:0，主机确实在输出 HDR10 时配 **HDR · PQ**。P010 传 SDR 时仍按 SDR 读。卡带不动 4K60 HDR 的话，我会先试 1440p60 HDR。")
                    ].join("\n"),
                    "## " + qsTr("30 帧游戏，我还是建议采 60 帧"),
                    qsTr("我是 Veyra 的开发者。玩 GTA 6 这类 30 帧主机游戏，我会用下面这套设置。感谢大家一直在用，也把各种设备的实际效果告诉我。如果你之前用 OBS 或 PotPlayer 预览觉得有延迟，可以拿同一张卡、同一个档位在 Veyra 里试一下。"),
                    qsTr("为什么 30 帧游戏也采 60？60 帧采集约每 16.7 ms 取一次画面，30 帧约每 33.3 ms 取一次。新画面有机会更早被采到，实际延迟还看采集卡和驱动。"),
                    qsTr("举个例子。游戏原帧是 `ABCDEFG`，采到的可能是 `AABBCCDDEEFFGG`。相同的两张图之间补不出新的运动。在「补帧」里选 **60→30** 或 **自动去除**，让补帧从 `A→B→C` 这样的不同画面开始。锁 30 帧可先试 60→30，帧率经常变化就对照自动去除和实际内容帧率。"),
                    "## " + qsTr("补帧模型和档位怎么挑"),
                    qsTr("30 帧源我更喜欢 **VFG**。在我试过的场景里，低帧率用 DLSS 或 XeSS 更容易看到拖影和果冻感。我会先开 **VFG 中档 2X**，跑稳了再试高档。如果本机没有 VFG，就从可用后端的 2X 试起。"),
                    qsTr("VFG 高档在显卡跑得动时真挺丝滑，但开销很大。我用 RTX 5070 在 4K 实测，高档单次补帧约 **20 ms**，中档开销约是高档的 **1/3–1/4**。多转几次镜头，看看耗时和帧节奏，再决定哪一档适合你的显卡。"),
                    "## " + qsTr("超分，我常用这一档"),
                    qsTr("Veyra 处理手上这张已经渲染好的画面，DLSS SR 在这里能发挥的空间有限。有 NVIDIA 显卡的话，我更推荐 **RTX 视频超分 · 标准 · 中**，画面变清楚，开销也比较好控。"),
                    "## " + qsTr("我的采集延迟实测"),
                    qsTr("原帖里记录的大致量级，具体会随设备和采集档位变化。"),
                    "- 1440p60 NV12 · 10–20 ms\n- 4K60 NV12 · 30–40 ms\n- 4K30 P010 HDR · 80–100 ms",
                    qsTr("很在意手感，我会选 **1440p60 NV12 再超分**。想兼顾预算和 HDR 观感，可以在 SDR 采集后开 **RTX 视频 HDR**。如果目标是原生 4K60 HDR 采集，我会看 PCIe 卡。"),
                    "## " + qsTr("采集卡怎么挑"),
                    qsTr("我选卡先看它能把哪种画面采进电脑。同一张卡的 HDMI 直通规格和实际采集规格分开看。"),
                    [
                        "- **5 Gbps USB** · " + qsTr("我主要看 1440p60、4K30 的 SDR 档位。原帖举例") + " · MICHENSEN T800/T900, KUHAIMI 27P/26P, ACASIS 007 Pro/009 Pro, AVerMedia GC553 Pro/GC551G2, Elgato HD60 X.",
                        "- **10 Gbps USB** · " + qsTr("看 4K60 NV12、1440p60 HDR 或 4K30 HDR。原帖举例") + " · MICHENSEN T911, KUHAIMI G50, AVerMedia GC553G2, Elgato 4K X.",
                        "- **PCIe** · " + qsTr("想要 4K60 HDR，可以看这些卡") + " · AVerMedia GC573, Elgato 4K Pro."
                    ].join("\n"),
                    qsTr("我测过的同规格卡，延迟差通常在 5 ms 左右以内。先看你的分辨率、帧率和色彩模式能不能跑稳，再选牌子。祝你玩得开心！")
                ].join("\n\n") : ""
            }
        }

        VButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("阅读 Reddit 原帖")
            ghost: true
            onClicked: Qt.openUrlExternally("https://www.reddit.com/r/GTA6unmoderated/comments/1xy9xff/how_to_properly_use_frame_generation_for_gta_6/")
        }
    }
}
