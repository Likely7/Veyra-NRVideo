import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: panel
    objectName: "fullscreen-timing-panel"
    readonly property bool videoCover: true
    readonly property real coverRadius: radius
    property bool expanded: true
    property real maximumHeight: 600
    signal closeRequested()
    height: Math.min(panel.maximumHeight, body.implicitHeight + 22)
    radius: 12
    color: Theme.card
    border.color: Theme.stroke2

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Flickable {
        id: scroll
        anchors.fill: parent
        anchors.margins: 11
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true
        interactive: contentHeight > height
        ScrollBar.vertical: VScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: body
            width: scroll.width - (scroll.interactive ? 10 : 0)
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                VButton {
                    Layout.fillWidth: true
                    text: qsTr("性能与耗时") + (panel.expanded ? "  ▾" : "  ▸")
                    onClicked: panel.expanded = !panel.expanded
                }
                VButton {
                    text: qsTr("%1 关闭").arg("Home")
                    onClicked: panel.closeRequested()
                }
            }
            Text {
                text: veyra.submitFpsLabel + " " + (veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) + " FPS" : "—")
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            ProPerfOrbs {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.expanded
                size: Math.max(32, Math.min(44, (body.width - 40) / 3))
                spacing: 6
            }
            Text {
                visible: panel.expanded
                text: veyra.stageBudgetMs > 0 ? qsTr("源帧预算 %1 ms").arg(veyra.stageBudgetMs.toFixed(2)) : qsTr("源帧预算 —")
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            VEyebrow { visible: panel.expanded; text: qsTr("GPU · 平均 / P95") }
            Repeater {
                model: panel.visible && panel.expanded ? veyra.stageTimings.length : 0
                delegate: TimingRow {
                    required property int index
                    readonly property var timing: veyra.stageTimings[index] || ({})
                    label: timing.label || ""
                    value: timing.measured ? timing.ms.toFixed(2) + " / " + timing.p95.toFixed(2) + " ms" : "—"
                }
            }
            VEyebrow { visible: panel.expanded; text: qsTr("CPU · P95") }
            Repeater {
                model: panel.visible && panel.expanded ? veyra.cpuTimings.length : 0
                delegate: TimingRow {
                    required property int index
                    readonly property var timing: veyra.cpuTimings[index] || ({})
                    label: timing.label || ""
                    value: timing.measured ? timing.p95.toFixed(2) + " ms" : "—"
                }
            }
        }
    }
    component TimingRow: RowLayout {
        required property string label
        required property string value
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: parent.label
            elide: Text.ElideRight
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
        }
        Text {
            text: parent.value
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSmall
        }
    }
}
