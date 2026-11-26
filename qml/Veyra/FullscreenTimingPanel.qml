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
    height: Math.min(panel.maximumHeight, body.implicitHeight + Theme.s24)
    radius: Theme.rModal
    color: Theme.card
    border.color: Theme.stroke2

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Flickable {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.s12
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true
        interactive: contentHeight > height
        ScrollBar.vertical: VScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: body
            width: scroll.width - (scroll.interactive ? Theme.s12 : 0)
            spacing: Theme.s4
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s8
                VButton {
                    Layout.fillWidth: true
                    iconName: panel.expanded ? "chevDown" : "chevRight"
                    text: qsTr("性能与耗时")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("展开或收起性能与耗时")
                    onClicked: panel.expanded = !panel.expanded
                }
                VButton {
                    icon: true
                    iconName: "x"
                    ghost: true
                    tip: qsTr("关闭面板 (Home)")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("关闭性能与耗时面板")
                    onClicked: panel.closeRequested()
                }
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: veyra.submitFpsLabel + " " + (veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) + " FPS" : "—")
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            ProPerfOrbs {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.expanded
                size: Math.max(32, Math.min(44, (body.width - 40) / 3))
                spacing: Theme.s8
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
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
