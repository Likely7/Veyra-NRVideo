// Window chrome: a labeled 40px app bar on work pages, and the same bar as a
// hover overlay on 极简 / fullscreen so the picture stays full-bleed.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dockRoot
    z: 40
    anchors.top: parent ? parent.top : undefined
    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    height: persistent ? Theme.appBarHeight : 12 + Theme.appBarHeight

    required property string currentPage
    signal requestPage(string page)
    property bool maximized: false
    signal requestMinimize()
    signal requestMaximize()
    signal requestClose()
    signal requestMove()
    property bool pillToggleVisible: false
    property bool pillHidden: false
    signal requestTogglePill()

    property bool persistent: true
    property bool opened: false
    property bool pinned: false
    property string forcedTip: ""
    readonly property bool shown: persistent || pinned || opened
    readonly property real barY: bar.y

    readonly property var items: [
        { id: "home", icon: "home", tip: qsTr("首页") },
        { id: "min",  icon: "tv", tip: qsTr("极简") },
        { id: "pro",  icon: "sliders", tip: qsTr("专业模式") },
        { id: "exp",  icon: "upload", tip: qsTr("导出") },
        { id: "set",  icon: "settings", tip: qsTr("设置") }
    ]

    function open() { retract.stop(); if (!locked && !persistent) opened = true }
    property bool locked: false
    Timer {
        id: retract
        interval: 450
        onTriggered: if (!dockRoot.pinned && !dockRoot.persistent) dockRoot.opened = false
    }

    Item {
        id: zone
        visible: !dockRoot.persistent
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.open(); else if (!barHover.hovered) retract.restart() }
        DragHandler { target: null; onActiveChanged: if (active) dockRoot.requestMove() }
        TapHandler { onDoubleTapped: dockRoot.requestMaximize() }
    }
    Rectangle {
        id: handle
        visible: !dockRoot.persistent
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 5
        width: dockRoot.shown ? 18 : 44
        height: 4
        radius: 2
        color: Qt.rgba(1, 1, 1, 0.28)
        opacity: dockRoot.shown ? 0 : 1
        Behavior on width { NumberAnimation { duration: Theme.d(Theme.durFast); easing.bezierCurve: Theme.easeOut } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.durFast) } }
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.open() }
    }

    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.appBarHeight
        y: dockRoot.shown ? 0 : -height
        color: Theme.shade(12, 12, 16)
        border.width: 0
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.stroke
        }
        objectName: "videoCover"
        property real coverRadius: 0
        Behavior on y {
            enabled: !dockRoot.persistent
            NumberAnimation { duration: Theme.d(Theme.durNormal); easing.bezierCurve: Theme.easeOut }
        }

        HoverHandler {
            id: barHover
            onHoveredChanged: {
                if (dockRoot.persistent) return
                if (hovered) dockRoot.open()
                else retract.restart()
            }
        }
        DragHandler {
            objectName: "dock-move"
            target: null
            onActiveChanged: if (active) dockRoot.requestMove()
        }
        TapHandler { onDoubleTapped: dockRoot.requestMaximize() }

        RowLayout {
            id: content
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            spacing: 4

            Item {
                implicitWidth: 28
                implicitHeight: 28
                Layout.alignment: Qt.AlignVCenter
                Image {
                    anchors.centerIn: parent
                    source: Theme.logoImage
                    sourceSize.width: 20
                    fillMode: Image.PreserveAspectFit
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: dockRoot.requestPage("home") }
            }

            Item {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: itemsRow.implicitWidth
                implicitHeight: 28
                Rectangle {
                    id: indicator
                    height: 26
                    radius: 6
                    color: Theme.card3
                    x: {
                        for (let i = 0; i < itemsRow.children.length; ++i) {
                            const c = itemsRow.children[i]
                            if (c.modelData && c.modelData.id === dockRoot.currentPage)
                                return c.x
                        }
                        return 0
                    }
                    width: {
                        for (let i = 0; i < itemsRow.children.length; ++i) {
                            const c = itemsRow.children[i]
                            if (c.modelData && c.modelData.id === dockRoot.currentPage)
                                return c.width
                        }
                        return 0
                    }
                    visible: width > 0
                    Behavior on x { NumberAnimation { duration: Theme.d(Theme.durFast); easing.bezierCurve: Theme.easeOut } }
                    Behavior on width { NumberAnimation { duration: Theme.d(Theme.durFast); easing.bezierCurve: Theme.easeOut } }
                }
                Row {
                    id: itemsRow
                    spacing: 2
                    Repeater {
                        model: dockRoot.items
                        delegate: Item {
                            id: btn
                            required property var modelData
                            width: Math.min(tabLabel.implicitWidth + (dockRoot.width >= 800 ? 36 : 16), Math.max(36, (dockRoot.width - 210) / dockRoot.items.length))
                            activeFocusOnTab: dockRoot.shown
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.tip
                            Accessible.onPressAction: dockRoot.requestPage(modelData.id)
                            Keys.onReturnPressed: dockRoot.requestPage(modelData.id)
                            Keys.onEnterPressed: dockRoot.requestPage(modelData.id)
                            Keys.onSpacePressed: dockRoot.requestPage(modelData.id)
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.rCtl
                                color: "transparent"
                                border.width: parent.activeFocus ? 1 : 0
                                border.color: Theme.accent
                            }
                            ToolTip.visible: btnHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: modelData.tip
                            height: 28
                            Row {
                                id: tabRow
                                anchors.centerIn: parent
                                spacing: 6
                                VIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: dockRoot.width >= 800
                                    name: btn.modelData.icon
                                    size: 14
                                    strokeWidth: 1.7
                                    color: Theme.t1
                                    opacity: dockRoot.currentPage === btn.modelData.id || btnHover.hovered ? 1.0 : 0.55
                                }
                                Text {
                                    id: tabLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, Math.max(1, btn.width - (dockRoot.width >= 800 ? 36 : 16)))
                                    elide: Text.ElideRight
                                    text: btn.modelData.tip
                                    color: Theme.t1
                                    opacity: dockRoot.currentPage === btn.modelData.id || btnHover.hovered ? 1.0 : 0.7
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsBody
                                    font.weight: dockRoot.currentPage === btn.modelData.id ? Font.Medium : Font.Normal
                                }
                            }
                            VDot {
                                visible: btn.modelData.id === "exp" && veyra.exportRunning
                                warn: true
                                x: btn.width - width - 2
                                y: 2
                                width: 6; height: 6; radius: 3
                            }
                            HoverHandler { id: btnHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: dockRoot.requestPage(btn.modelData.id) }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Item {
                objectName: "dock-pill-toggle"
                visible: dockRoot.pillToggleVisible
                implicitWidth: visible ? 28 : 0
                implicitHeight: 28
                Layout.alignment: Qt.AlignVCenter
                VIcon {
                    anchors.centerIn: parent
                    name: dockRoot.pillHidden ? "eye" : "eyeoff"
                    color: Theme.t1
                    opacity: pillHover.hovered ? 1.0 : 0.55
                }
                ToolTip.visible: pillHover.hovered
                ToolTip.text: dockRoot.pillHidden ? qsTr("显示播放条") : qsTr("隐藏播放条")
                HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: dockRoot.requestTogglePill() }
            }

            Item {
                implicitWidth: 16
                implicitHeight: 28
                Layout.alignment: Qt.AlignVCenter
                VDot { anchors.centerIn: parent; off: !veyra.running; warn: veyra.captureRecovering }
            }

            Repeater {
                model: [
                    { id: "min", icon: "minus", tip: qsTr("最小化") },
                    { id: "max", icon: "max", tip: dockRoot.maximized ? qsTr("还原") : qsTr("最大化") },
                    { id: "close", icon: "x", tip: qsTr("关闭") }
                ]
                delegate: Item {
                    id: wbtn
                    required property var modelData
                    objectName: "window-" + modelData.id
                    implicitWidth: 32
                    implicitHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: whover.hovered ? (wbtn.modelData.id === "close" ? Qt.rgba(1, 0.365, 0.365, 0.22) : Theme.card3) : "transparent"
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: wbtn.modelData.icon
                        size: 13
                        color: whover.hovered && wbtn.modelData.id === "close" ? "#FF8A8A" : Theme.t1
                        opacity: whover.hovered ? 1.0 : 0.55
                    }
                    ToolTip.visible: whover.hovered
                    ToolTip.text: wbtn.modelData.tip
                    HoverHandler { id: whover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
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
