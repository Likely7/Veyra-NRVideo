// The top dock: hidden until the pointer reaches the top edge, then it drops in.
//
// The user asked for this specifically (the old always-visible sidebar was too
// large and crowded), so the hit zone is a thin strip at the very top and the
// dock retracts on its own after a few idle seconds. It never covers the video
// for longer than it takes to pick a page.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dockRoot
    // Sits above the pages without stealing input from them.
    z: 30
    width: parent ? parent.width : 0
    height: pointerZone.height + bar.height + 16

    required property string currentPage
    signal requestPage(string page)

    // How long the dock stays out after the last interaction.
    readonly property int idleMs: 3200
    property bool opened: false
    // Pinned open while a menu inside it is showing, so it cannot retract out
    // from under the user mid-click.
    property bool pinned: false

    // The strip that summons the dock. Thin, or it eats clicks meant for the
    // page underneath.
    Item {
        id: pointerZone
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.opened = true }
    }

    Timer {
        id: retractTimer
        interval: dockRoot.idleMs
        onTriggered: if (!dockRoot.pinned) dockRoot.opened = false
    }
    onOpenedChanged: if (opened) retractTimer.restart()

    HoverHandler {
        onHoveredChanged: {
            if (hovered) {
                dockRoot.opened = true
                retractTimer.restart()
            } else {
                retractTimer.restart()
            }
        }
    }

    // The grab handle stays visible when the dock is away: without it there is
    // nothing to tell the user the dock exists.
    Rectangle {
        id: handle
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 5
        width: dockRoot.opened ? 18 : 44
        height: 4
        radius: 4
        color: Theme.text3
        opacity: dockRoot.opened ? 0 : 1
        Behavior on width { NumberAnimation { duration: Theme.durationSlow; easing.bezierCurve: Theme.spring } }
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    Rectangle {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: dockRoot.opened ? 8 : -bar.height - 8
        width: content.implicitWidth + 16
        height: 44
        radius: height / 2
        color: "#000000"
        border.width: 1
        border.color: Theme.stroke2
        // The drop-in/retract spring is the whole feel of this control.
        Behavior on anchors.topMargin {
            NumberAnimation { duration: Theme.durationSlow; easing.bezierCurve: Theme.spring }
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 2

            // The logo returns to the empty state, as asked. It is the only way
            // back to home from a zoomed-in page, so it is always available.
            Item {
                width: 40
                height: 30
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: logoHover.hovered ? Theme.card2 : "transparent"
                    enabled: false
                    id: logoBg
                }
                Text {
                    anchors.centerIn: parent
                    text: "V"
                    color: Theme.accent
                    font.family: Theme.fontUi
                    font.pixelSize: 18
                    font.bold: true
                }
                HoverHandler { id: logoHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: dockRoot.requestPage("home") }
            }

            Repeater {
                model: [
                    { id: "home", label: "主页" },
                    { id: "minimal", label: "极简" },
                    { id: "pro", label: "专业" },
                    { id: "node", label: "节点" },
                    { id: "export", label: "导出" },
                    { id: "settings", label: "设置" }
                ]
                delegate: Item {
                    required property var modelData
                    implicitWidth: tabText.implicitWidth + 22
                    implicitHeight: 30
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: dockRoot.currentPage === modelData.id ? Theme.card3
                             : tabHover.hovered ? Theme.card2 : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: dockRoot.currentPage === modelData.id ? Theme.text1 : Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeBody
                    }
                    HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: dockRoot.requestPage(modelData.id) }
                }
            }
        }
    }
}
