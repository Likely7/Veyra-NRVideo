// The PC stream's own numbers (Ctrl+Alt+Shift+S while streaming): what the host and the network
// contribute before the picture reaches the software's effect chain. The software's own processing
// time is on the performance readouts of the professional page; nothing here is invented: a value the
// host does not report (older Sunshine has no host latency) shows as a dash.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: hud
    objectName: "stream-hud"
    readonly property var ml: veyra.moonlight
    readonly property var s: ml && ml.state.stream ? ml.state.stream : ({})
    readonly property bool shown: !!ml && ml.state.streaming === true && veyra.moonlightStatsVisible
    // Cut out of the native video window like the dialogs, so it is readable over the picture.
    readonly property bool videoCover: true
    property real coverRadius: 10

    function ms(v) { return v > 0 ? v.toFixed(1) + " ms" : "—" }
    function num(v) { return v > 0 ? v.toFixed(1) : "—" }

    visible: shown
    width: col.implicitWidth + 28
    height: col.implicitHeight + 24
    radius: 10
    color: Qt.rgba(0.04, 0.04, 0.05, 0.86)
    border.width: 1
    border.color: Theme.stroke2
    ColumnLayout {
        id: col
        anchors.centerIn: parent
        spacing: 4
        Text {
            text: (s.width > 0 ? s.width + "×" + s.height : "") + " · " + (s.codec || "") + (s.hdr ? " HDR" : "")
                + " · " + (s.hardware ? "硬件解码" : "软件解码")
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: 12
            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
        }
        Text {
            text: "主机 " + hud.ms(s.hostMs) + "  ·  往返 " + hud.ms(s.rttMs) + "  ·  接收 " + hud.ms(s.receiveMs)
                + "  ·  排队 " + hud.ms(s.queueMs) + "  ·  解码 " + hud.ms(s.decodeMs)
            color: Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            text: "收 " + hud.num(s.receivedFps) + " / 解 " + hud.num(s.decodedFps) + " fps  ·  包 " + (s.packets || 0)
                + "  ·  FEC 恢复 " + (s.recovered || 0) + "  ·  未恢复 " + (s.lost || 0) + "  ·  丢帧 " + (s.dropped || 0)
            color: (s.lost || 0) > 0 ? Theme.warn : Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            text: veyra.moonlightCaptured ? "键盘鼠标已交给主机 · Ctrl+Alt+Shift+Z 释放 · +Q 断开" : "键盘鼠标未捕获 · 点击画面捕获"
            color: veyra.moonlightCaptured ? Theme.ok : Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: 11
        }
    }
}
