// The top dock, rebuilt from the design (index.html .dock-zone/.dock/.dock-handle
// and app.css's .dock* rules).
//
// Design facts this follows:
//   * a 12px hit zone plus a 44x4 grab handle at the very top;
//   * the dock itself is a black pill, 40px tall buttons with SVG icons and a
//     tooltip; a separate indicator slides behind the selected button;
//   * it drops in from above and retracts 450ms after the pointer leaves;
//   * `dockPinned` keeps it open (the design uses that state for the 16:9 frame).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dockRoot
    z: 40
    anchors.top: parent ? parent.top : undefined
    width: parent ? parent.width : 0
    height: 12 + 46 + 20

    required property string currentPage
    signal requestPage(string page)

    property bool opened: false
    // Mirrors st.dockPinned in the design: pinned stays open regardless of hover.
    property bool pinned: false

    // --- page definitions (order and tooltips from core.js) ---------------
    readonly property var items: [
        { id: "min",   icon: "🎬", tip: "极简" },
        { id: "pro",   icon: "⚙",  tip: "专业模式" },
        { id: "exp",   icon: "⬇",  tip: "导出" },
        { id: "set",   icon: "☰",  tip: "设置" }
    ]

    // .dock-zone { height:12px } and .dock-handle { 44x4, top:5 }
    Item {
        id: zone
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.opened = true }
    }
    Rectangle {
        id: handle
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 5
        width: dockRoot.opened ? 18 : 44
        height: 4
        radius: 4
        color: Qt.rgba(1, 1, 1, 0.28)
        opacity: dockRoot.opened ? 0 : 1
        Behavior on width { NumberAnimation { duration: 400; easing.bezierCurve: Theme.spring } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.opened = true }
    }

    // Retract 450ms after the pointer leaves, exactly like closeDock().
    Timer {
        id: retract
        interval: 450
        onTriggered: if (!dockRoot.pinned) dockRoot.opened = false
    }
    onOpenedChanged: if (!opened) ; else retract.stop()

    // .dock: black pill, radius 999, 1px hairline, heavy drop shadow.
    Rectangle {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: dockRoot.opened ? 8 : -56
        implicitWidth: content.implicitWidth + 20
        height: 44
        radius: height / 2
        color: "#000000"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
        // .dock { transform: translate(-50%,-120%); transition: transform .55s spring }
        Behavior on anchors.topMargin {
            NumberAnimation { duration: 550; easing.bezierCurve: Theme.spring }
        }

        HoverHandler {
            onHoveredChanged: {
                if (hovered) { dockRoot.opened = true; retract.stop() }
                else retract.restart()
            }
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 2

            // .dock .logo: 40x30, returns to the empty state.
            Item {
                implicitWidth: 40
                implicitHeight: 30
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: logoHover.hovered ? "#1A1A1F" : "transparent"
                }
                Image {
                    anchors.centerIn: parent
                    source: "logo.png"
                    sourceSize.width: 26
                    fillMode: Image.PreserveAspectFit
                }
                HoverHandler { id: logoHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: dockRoot.requestPage("home")
                    onPressedChanged: if (pressed) scale = 0.9
                }
            }

            // .dock-sep
            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Qt.rgba(1, 1, 1, 0.14) }

            // .dock-items with the sliding .dock-ind behind the active button.
            Item {
                implicitWidth: itemsRow.implicitWidth
                implicitHeight: 30
                Rectangle {
                    id: indicator
                    width: 32
                    height: 30
                    radius: 999
                    color: "#26262D"
                    x: {
                        let idx = 0
                        for (let i = 0; i < dockRoot.items.length; ++i)
                            if (dockRoot.items[i].id === dockRoot.currentPage) idx = i
                        return idx * 34
                    }
                    visible: dockRoot.currentPage !== "home"
                    // .dock-ind { transition: transform .55s var(--spring) }
                    Behavior on x { NumberAnimation { duration: 550; easing.bezierCurve: Theme.spring } }
                }
                Row {
                    id: itemsRow
                    spacing: 2
                    Repeater {
                        model: dockRoot.items
                        delegate: Item {
                            required property var modelData
                            width: 32
                            height: 30
                            Text {
                                anchors.centerIn: parent
                                text: modelData.icon
                                font.pixelSize: 15
                                opacity: dockRoot.currentPage === modelData.id ? 1.0 : 0.45
                                Behavior on opacity { NumberAnimation { duration: 200 } }
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: dockRoot.requestPage(modelData.id) }
                        }
                    }
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Qt.rgba(1, 1, 1, 0.14) }

            // .dock-live: the engine-running lamp. Lit only when the engine really
            // reports running; no decorative lamp.
            Item {
                implicitWidth: 19
                implicitHeight: 30
                VDot { anchors.centerIn: parent; off: !veyra.running; warn: veyra.captureRecovering }
            }
        }
    }
}
