// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: orb
    property real fraction: 0
    property bool known: false
    property string value: "—"
    property string label: ""
    property string tip: ""
    property int size: 56
    // Compact (the node page's 40px bar): no label under the meter; the
    // tooltip names the metric instead.
    property bool showLabel: true
    readonly property color tone: !known ? Qt.rgba(1, 1, 1, 0.25)
                                : fraction < 0.7 ? Theme.ok : fraction < 1.0 ? "#F2B941" : Theme.err
    implicitWidth: Math.max(size, labelText.implicitWidth)
    implicitHeight: showLabel ? size + 4 + labelText.implicitHeight : size
    // The meter is only a few dozen pixels across. Sub-percent changes do
    // not add visible information; the numeric label retains full precision.
    readonly property real shown: Math.round(telemetry.value * 100) / 100
    VTelemetryValue {
        id: telemetry
        targetValue: orb.known ? Math.min(1, Math.max(0, orb.fraction)) : 0
        animate: orb.visible && !Theme.reduced && (!veyra.paused || (typeof vyTest !== "undefined" && vyTest.disablePausedUiIdle === true))
        duration: 600
    }
    Item {
        id: ringBox
        width: orb.size; height: orb.size
        anchors.horizontalCenter: parent.horizontalCenter
        Text {
            anchors.centerIn: parent
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: orb.value
            color: orb.known ? Theme.t1 : Theme.t3
            font.family: Theme.fontMono
            font.pixelSize: orb.size >= 50 ? 13 : 11
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 3
            color: Theme.stroke
            Rectangle {
                width: parent.width * orb.shown
                height: parent.height
                color: orb.tone
                visible: orb.known
            }
        }
    }
    Text {
        id: labelText
        visible: orb.showLabel
        anchors.top: ringBox.bottom; anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        text: orb.label
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 10
    }
    HoverHandler { id: orbHover }
    ToolTip.visible: orbHover.hovered && (orb.tip.length > 0 || !orb.showLabel)
    ToolTip.text: (orb.showLabel ? "" : orb.label + "：") + orb.tip
}
