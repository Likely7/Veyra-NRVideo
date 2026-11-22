// The stream's own numbers (Ctrl+Alt+Shift+S while streaming): what the host and the network contribute
// before the picture reaches the software's effect chain, for PC (Moonlight) and Xbox streams. The
// software's own processing time is on the performance readouts of the professional page; nothing here is
// invented: a value the stream does not report shows as a dash.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: hud
    objectName: "stream-hud"
    readonly property var ml: veyra.moonlight
    readonly property var xb: veyra.xbox
    readonly property bool xboxOn: !!xb && xb.state.streaming === true
    readonly property bool pcOn: !!ml && ml.state.streaming === true
    readonly property var s: xboxOn ? (xb.state.stream || ({})) : pcOn ? (ml.state.stream || ({})) : ({})
    // PS5: veyra.ps5.stream, refreshed once a second while this overlay is shown.
    readonly property var p: veyra.ps5.stream || ({})
    readonly property bool ps5On: !xboxOn && !pcOn && veyra.sourceKind === "ps5" && veyra.ps5.active === true
    readonly property bool shown: (xboxOn || pcOn || ps5On) && veyra.moonlightStatsVisible
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
            visible: hud.ps5On
            text: "PS5 · " + (p.width > 0 ? p.width + "×" + p.height + "@" + p.fps : "") + " · " + (p.hardware ? qsTr("硬件解码") : qsTr("软件解码"))
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: 12
            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
        }
        Text {
            visible: hud.ps5On
            text: qsTr("码率 ") + (p.videoMbps > 0 ? p.videoMbps.toFixed(1) + " / " + p.requestMbps + " Mbps" : "—") + qsTr("  ·  解码 ") + hud.ms(p.decodeMs)
            color: Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            visible: hud.ps5On
            // Packets are Chiaki's current window, which it resets itself: a rate, not a session total.
            text: qsTr("收 ") + hud.num(p.receivedFps) + qsTr(" / 解 ") + hud.num(p.decodedFps) + " / out " + (veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : "—")
                + qsTr(" fps  ·  包 ") + (p.packets || 0) + qsTr("  ·  未恢复 ") + (p.packetsLost || 0) + qsTr("  ·  丢帧 ") + ((p.dropped || 0) + (p.framesLost || 0))
            color: (p.packetsLost || 0) > 0 ? Theme.warn : Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            visible: hud.ps5On && veyra.ps5.mic === true
            text: (veyra.ps5.micMuted ? "Mic muted" : "Mic live") + " · Ctrl+Alt+Shift+M"
            color: veyra.ps5.micMuted ? Theme.warn : Theme.ok
            font.family: Theme.fontUi
            font.pixelSize: 11
        }
        Text {
            visible: !hud.ps5On
            text: (hud.xboxOn ? "Xbox · " : "PC · ") + (s.width > 0 ? s.width + "×" + s.height : "") + " · " + (s.codec || "")
                + (s.hdr ? " HDR" : "") + " · " + (s.hardware ? qsTr("硬件解码") : qsTr("软件解码"))
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: 12
            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
        }
        Text {
            visible: hud.pcOn && !hud.xboxOn
            text: qsTr("主机 ") + hud.ms(s.hostMs) + qsTr("  ·  往返 ") + hud.ms(s.rttMs) + qsTr("  ·  接收 ") + hud.ms(s.receiveMs)
                + qsTr("  ·  排队 ") + hud.ms(s.queueMs) + qsTr("  ·  解码 ") + hud.ms(s.decodeMs)
                + qsTr("  ·  码率 ") + (s.videoMbps > 0 ? s.videoMbps.toFixed(1) + " Mbps" : "—")
            color: Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            visible: hud.xboxOn
            text: qsTr("往返 ") + hud.ms(s.rttMs) + qsTr("  ·  解码 ") + hud.ms(s.decodeMs) + qsTr("  ·  码率 ") + (s.videoMbps > 0 ? s.videoMbps.toFixed(1) + " Mbps" : "—")
            color: Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            visible: !hud.ps5On
            text: hud.xboxOn
                ? qsTr("收 ") + hud.num(s.receivedFps) + qsTr(" / 解 ") + hud.num(s.decodedFps) + qsTr(" fps  ·  丢帧 ") + (s.dropped || 0) + qsTr("  ·  关键帧请求 ") + (s.keyframes || 0)
                : qsTr("收 ") + hud.num(s.receivedFps) + qsTr(" / 解 ") + hud.num(s.decodedFps) + qsTr(" fps  ·  包 ") + (s.packets || 0)
                  + qsTr("  ·  FEC 恢复 ") + (s.recovered || 0) + qsTr("  ·  未恢复 ") + (s.lost || 0) + qsTr("  ·  丢帧 ") + (s.dropped || 0)
            color: (s.lost || 0) > 0 ? Theme.warn : Theme.t2
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
        Text {
            text: hud.xboxOn || hud.ps5On ? qsTr("Ctrl+Alt+Shift+S 隐藏") :
                  veyra.moonlightCaptured ? qsTr("键盘鼠标已交给主机 · Ctrl+Alt+Shift+Z 释放 · +Q 断开") : qsTr("键盘鼠标未捕获 · 点击画面捕获")
            color: !hud.xboxOn && veyra.moonlightCaptured ? Theme.ok : Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: 11
        }
    }
}
