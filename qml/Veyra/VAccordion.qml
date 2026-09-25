// .acc — the accordion card the design's inspector is built from.
//
// From pages.css: a card with a header row (colour-plated icon, title, a summary
// line under it, optional toggle, chevron) and a body that expands. The chevron
// rotates 90 degrees when open, and the body animates its height rather than
// appearing instantly.
//
// `groups` renders the design's nested collapsible sub-panels (模型参数 6 项,
// 增强变化量 5 项, 实验 2 项), each with an item count on the right.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: acc
    property string title: ""
    property string summary: ""
    property string glyph: ""
    property color hue: Theme.accent
    property bool enabledSwitch: true
    property bool on: true
    property bool open: false
    // [{ label, count, open }] — nested groups, as the design draws them.
    property var groups: []
    signal toggled(bool on)
    signal headerClicked()
    // Caller-supplied rows land in the body. Declared here, on the root, because
    // a default property belongs to the component it is declared in.
    default property alias content: body.data

    readonly property int headerHeight: 48
    implicitHeight: headerHeight + (open ? body.implicitHeight + 8 : 0)
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: open ? Theme.stroke2 : Theme.stroke
    Behavior on implicitHeight { NumberAnimation { duration: 500; easing.bezierCurve: Theme.springSoft } }
    clip: true

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 0

        // --- header ------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: acc.headerHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 9

                // .sico: a 26px plate tinted by the effect's own hue.
                Rectangle {
                    implicitWidth: 26
                    implicitHeight: 26
                    radius: 8
                    color: Qt.rgba(acc.hue.r, acc.hue.g, acc.hue.b, 0.16)
                    Text {
                        anchors.centerIn: parent
                        text: acc.glyph
                        font.pixelSize: 13
                        color: acc.hue
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: acc.title
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: acc.summary.length > 0
                        text: acc.summary
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                VSwitch {
                    visible: acc.enabledSwitch
                    checked: acc.on
                    onToggled: acc.toggled(checked)
                }

                // .chev rotates 90 degrees when the card is open.
                Text {
                    text: "›"
                    color: Theme.t3
                    font.pixelSize: 16
                    rotation: acc.open ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 450; easing.bezierCurve: Theme.spring } }
                }
            }

            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { acc.open = !acc.open; acc.headerClicked() } }
        }

        // --- body --------------------------------------------------------
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 10
            spacing: 2
            visible: acc.open || implicitHeight > 0

            Repeater {
                model: acc.groups
                delegate: VSubGroup {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    label: modelData.label
                    count: modelData.count
                    expanded: modelData.open === true
                }
            }
        }
    }

    // A hairline between the header and the body, as in the design.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: acc.headerHeight
        height: acc.open ? 1 : 0
        color: Theme.stroke
        opacity: acc.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
