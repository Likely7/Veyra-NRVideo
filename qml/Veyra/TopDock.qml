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
    // Frameless window controls live in the capsule (user decision 2026-09-28).
    property bool maximized: false
    signal requestMinimize()
    signal requestMaximize()
    signal requestClose()
    signal requestMove()
    // The cinema pill's eye: shown on 极简 and in fullscreen, toggles the pill.
    property bool pillToggleVisible: false
    property bool pillHidden: false
    signal requestTogglePill()

    property bool opened: false
    // Mirrors st.dockPinned in the design: pinned stays open regardless of hover.
    property bool pinned: false
    // Review: holds one button's tooltip shown (--tip).
    property string forcedTip: ""
    // The pill's top edge, read by the motion probe (G0.5).
    readonly property real barY: bar.y

    // --- page definitions (order and tooltips from core.js) ---------------
    readonly property var items: [
        { id: "min",   icon: "tv", tip: "极简" },
        { id: "pro",   icon: "sliders", tip: "专业模式" },
        { id: "exp",   icon: "upload", tip: "导出" },
        { id: "set",   icon: "settings", tip: "设置" }
    ]

    // .dock-zone { height:12px } and .dock-handle { 44x4, top:5 }
    Item {
        id: zone
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        // Leaving the zone for anywhere but the dock retracts it (closeDock).
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.open(); else if (!barHover.hovered) retract.restart() }
        // The top strip is the title bar: drag moves, double click maximizes.
        DragHandler { target: null; onActiveChanged: if (active) dockRoot.requestMove() }
        TapHandler { onDoubleTapped: dockRoot.requestMaximize() }
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
        Behavior on width { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.open() }
    }

    function open() { retract.stop(); if (!locked) opened = true }
    // Fullscreen lock (Ctrl+L): the pointer no longer brings the dock down.
    property bool locked: false
    // Retract 450ms after the pointer leaves, exactly like closeDock().
    Timer {
        id: retract
        interval: 450
        onTriggered: if (!dockRoot.pinned) dockRoot.opened = false
    }

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
        // Over the picture on the cinema page: cut out of the video window.
        objectName: "videoCover"
        property real coverRadius: height / 2
        // .dock { transform: translate(-50%,-120%); transition: transform .55s spring }
        Behavior on anchors.topMargin {
            NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring }
        }

        HoverHandler {
            id: barHover
            onHoveredChanged: if (hovered) dockRoot.open(); else retract.restart()
        }
        // Dragging the capsule moves the window, as a title bar would.
        DragHandler {
            objectName: "dock-move"
            target: null
            onActiveChanged: if (active) dockRoot.requestMove()
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 2

            // .dock .logo: 40x30, returns to the empty state.
            // M3: background .2s, :active scale .9 over .4s --spring.
            Item {
                implicitWidth: 40
                implicitHeight: 30
                scale: logoTap.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: logoHover.hovered ? "#1A1A1F" : Qt.rgba(0.102, 0.102, 0.122, 0)
                    Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                }
                Image {
                    anchors.centerIn: parent
                    source: "logo.png"
                    sourceSize.width: 26
                    fillMode: Image.PreserveAspectFit
                }
                HoverHandler { id: logoHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: logoTap
                    onTapped: dockRoot.requestPage("home")
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
                    Behavior on x { NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
                }
                Row {
                    id: itemsRow
                    spacing: 2
                    Repeater {
                        model: dockRoot.items
                        delegate: Item {
                            id: btn
                            required property var modelData
                            width: 32
                            height: 30
                            // M4: .dock-btn:active scale .86 over .45s --spring.
                            scale: btnTap.pressed ? 0.86 : 1
                            Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                            // .dock-btn: color rgba(255,255,255,.45), hover or selected #fff.
                            VIcon {
                                anchors.centerIn: parent
                                name: btn.modelData.icon
                                color: "#FFFFFF"
                                opacity: dockRoot.currentPage === btn.modelData.id || tip.on ? 1.0 : 0.45
                                Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
                            }
                            // M5: .tip 38px below, opacity .15s, scale .85 -> 1 over .3s
                            // --spring from its top centre.
                            Rectangle {
                                id: tip
                                readonly property bool on: btnHover.hovered || dockRoot.forcedTip === btn.modelData.id
                                // Below the dock it sits over the video: cut out of it.
                                objectName: "videoCover"
                                property real coverRadius: 7
                                x: (btn.width - width) / 2
                                y: 38
                                width: tipText.implicitWidth + 16 + 2
                                height: tipText.implicitHeight + 8 + 2
                                radius: 7
                                color: Theme.popover
                                border.width: 1
                                border.color: Theme.stroke2
                                opacity: on ? 1 : 0
                                visible: opacity > 0
                                scale: on ? 1 : 0.85
                                transformOrigin: Item.Top
                                Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                                Behavior on scale { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
                                Text {
                                    id: tipText
                                    anchors.centerIn: parent
                                    text: btn.modelData.tip
                                    color: "#FFFFFF"
                                    font.family: Theme.fontUi
                                    font.pixelSize: 11   // 11.5px; pixelSize is an int (fractional sizes: G3)
                                }
                            }
                            // Export badge: a running export marks the export button
                            // with the design's pulsing .dot.warn; engine state only.
                            VDot {
                                visible: btn.modelData.id === "exp" && veyra.exportRunning
                                warn: true
                                x: btn.width - width - 3
                                y: 3
                                width: 6; height: 6; radius: 3
                            }
                            HoverHandler { id: btnHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { id: btnTap; onTapped: dockRoot.requestPage(btn.modelData.id) }
                        }
                    }
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Qt.rgba(1, 1, 1, 0.14) }

            // Show / hide the cinema pill (field request 2026-10-01). Only where the pill lives.
            Item {
                objectName: "dock-pill-toggle"
                visible: dockRoot.pillToggleVisible
                implicitWidth: visible ? 32 : 0
                implicitHeight: 30
                scale: pillTap.pressed ? 0.86 : 1
                Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                VIcon {
                    anchors.centerIn: parent
                    name: dockRoot.pillHidden ? "eye" : "eyeoff"
                    color: "#FFFFFF"
                    opacity: pillHover.hovered ? 1.0 : 0.45
                }
                Rectangle {
                    objectName: "videoCover"
                    property real coverRadius: 7
                    readonly property bool on: pillHover.hovered
                    x: (parent.width - width) / 2
                    y: 38
                    width: pillTipText.implicitWidth + 18
                    height: pillTipText.implicitHeight + 10
                    radius: 7
                    color: Theme.popover
                    border.width: 1
                    border.color: Theme.stroke2
                    opacity: on ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                    Text {
                        id: pillTipText
                        anchors.centerIn: parent
                        text: dockRoot.pillHidden ? "显示播放条" : "隐藏播放条"
                        color: "#FFFFFF"
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                    }
                }
                HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: pillTap; onTapped: dockRoot.requestTogglePill() }
            }

            // .dock-live: the engine-running lamp. Lit only when the engine really
            // reports running; no decorative lamp.
            Item {
                implicitWidth: 19
                implicitHeight: 30
                VDot { anchors.centerIn: parent; off: !veyra.running; warn: veyra.captureRecovering }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Qt.rgba(1, 1, 1, 0.14) }

            // Window controls: minimize, maximize/restore, close.
            Repeater {
                model: [
                    { id: "min", icon: "minus", tip: "最小化" },
                    { id: "max", icon: "max", tip: dockRoot.maximized ? "还原" : "最大化" },
                    { id: "close", icon: "x", tip: "关闭" }
                ]
                delegate: Item {
                    id: wbtn
                    required property var modelData
                    objectName: "window-" + modelData.id
                    implicitWidth: 30
                    implicitHeight: 30
                    scale: wtap.pressed ? 0.86 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: whover.hovered ? (wbtn.modelData.id === "close" ? Qt.rgba(1, 0.365, 0.365, 0.22) : "#1A1A1F") : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: wbtn.modelData.icon
                        size: 13
                        color: whover.hovered && wbtn.modelData.id === "close" ? "#FF8A8A" : "#FFFFFF"
                        opacity: whover.hovered ? 1.0 : 0.45
                        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
                    }
                    Rectangle {
                        objectName: "videoCover"
                        property real coverRadius: 7
                        readonly property bool on: whover.hovered
                        x: (wbtn.width - width) / 2
                        y: 38
                        width: wtipText.implicitWidth + 18
                        height: wtipText.implicitHeight + 10
                        radius: 7
                        color: Theme.popover
                        border.width: 1
                        border.color: Theme.stroke2
                        opacity: on ? 1 : 0
                        visible: opacity > 0
                        scale: on ? 1 : 0.85
                        transformOrigin: Item.Top
                        Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                        Behavior on scale { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
                        Text {
                            id: wtipText
                            anchors.centerIn: parent
                            text: wbtn.modelData.tip
                            color: "#FFFFFF"
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
                    }
                    HoverHandler { id: whover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: wtap
                        onTapped: {
                            if (wbtn.modelData.id === "min") dockRoot.requestMinimize()
                            else if (wbtn.modelData.id === "max") dockRoot.requestMaximize()
                            else dockRoot.requestClose()
                        }
                    }
                }
            }
        }
    }
}
