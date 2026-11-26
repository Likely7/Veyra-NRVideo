// The top dock: 12px hit zone + 44x4 grab handle, black capsule with nav,
// engine status, optional pill toggle, and window controls.
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
    property bool maximized: false
    signal requestMinimize()
    signal requestMaximize()
    signal requestClose()
    signal requestMove()
    property bool pillToggleVisible: false
    property bool pillHidden: false
    signal requestTogglePill()

    property bool opened: false
    property bool pinned: false
    property string forcedTip: ""
    readonly property real barY: bar.y

    readonly property var items: [
        { id: "min",   icon: "tv", tip: qsTr("极简") },
        { id: "pro",   icon: "sliders", tip: qsTr("专业模式") },
        { id: "exp",   icon: "upload", tip: qsTr("导出") },
        { id: "set",   icon: "settings", tip: qsTr("设置") }
    ]

    component DockTip: Rectangle {
        id: dtip
        property bool shown: false
        property string text: ""
        objectName: "videoCover"
        property real coverRadius: Theme.rCtl
        x: parent ? (parent.width - width) / 2 : 0
        y: 38
        width: dtipText.implicitWidth + Theme.s16
        height: dtipText.implicitHeight + Theme.s8
        radius: Theme.rCtl
        color: Theme.popover
        border.width: 1
        border.color: Theme.stroke2
        opacity: shown ? 1 : 0
        visible: opacity > 0
        scale: shown ? 1 : 0.85
        transformOrigin: Item.Top
        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animFast) } }
        Behavior on scale { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
        Text {
            id: dtipText
            anchors.centerIn: parent
            text: dtip.text
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
        }
    }

    Item {
        id: zone
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
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 5
        width: dockRoot.opened ? 18 : 44
        height: 4
        radius: 4
        color: Theme.t3
        opacity: dockRoot.opened ? 0 : 0.7
        Behavior on width { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animNormal) } }
        HoverHandler { onHoveredChanged: if (hovered) dockRoot.open() }
    }

    function open() { retract.stop(); if (!locked) opened = true }
    property bool locked: false
    Timer {
        id: retract
        interval: 450
        onTriggered: if (!dockRoot.pinned) dockRoot.opened = false
    }

    Rectangle {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: dockRoot.opened ? Theme.s8 : -56
        implicitWidth: content.implicitWidth + Theme.s20
        height: 44
        radius: height / 2
        color: Theme.bg
        border.width: 1
        border.color: Theme.strokeStrong
        objectName: "videoCover"
        property real coverRadius: height / 2
        Behavior on anchors.topMargin {
            NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring }
        }

        HoverHandler {
            id: barHover
            onHoveredChanged: if (hovered) dockRoot.open(); else retract.restart()
        }
        DragHandler {
            objectName: "dock-move"
            target: null
            onActiveChanged: if (active) dockRoot.requestMove()
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 2

            Item {
                id: logoBtn
                implicitWidth: 40
                implicitHeight: 30
                activeFocusOnTab: dockRoot.opened
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("首页")
                Accessible.onPressAction: dockRoot.requestPage("home")
                Keys.onReturnPressed: dockRoot.requestPage("home")
                Keys.onEnterPressed: dockRoot.requestPage("home")
                Keys.onSpacePressed: dockRoot.requestPage("home")
                scale: logoTap.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: logoTap.pressed ? Theme.pressFill : logoHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: logoBtn.activeFocus ? 1.5 : 0
                    border.color: Theme.focusRing
                    Behavior on color { ColorAnimation { duration: Theme.d(Theme.animNormal) } }
                }
                Image {
                    anchors.centerIn: parent
                    source: Theme.logoImage
                    sourceSize.width: 26
                    fillMode: Image.PreserveAspectFit
                }
                HoverHandler { id: logoHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: logoTap
                    onTapped: dockRoot.requestPage("home")
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.strokeStrong }

            Item {
                implicitWidth: itemsRow.implicitWidth
                implicitHeight: 30
                Rectangle {
                    id: indicator
                    width: 32
                    height: 30
                    radius: height / 2
                    color: Theme.card3
                    x: {
                        for (let i = 0; i < dockRoot.items.length; ++i) {
                            if (dockRoot.items[i].id === dockRoot.currentPage && itemsRow.children[i])
                                return itemsRow.children[i].x
                        }
                        return 0
                    }
                    visible: dockRoot.currentPage !== "home"
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
                            activeFocusOnTab: dockRoot.opened
                            Accessible.role: Accessible.Button
                            Accessible.name: btn.modelData.tip
                            Accessible.onPressAction: dockRoot.requestPage(btn.modelData.id)
                            Keys.onReturnPressed: dockRoot.requestPage(btn.modelData.id)
                            Keys.onEnterPressed: dockRoot.requestPage(btn.modelData.id)
                            Keys.onSpacePressed: dockRoot.requestPage(btn.modelData.id)
                            scale: btnTap.pressed ? 0.86 : 1
                            Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                            Rectangle {
                                anchors.fill: parent
                                radius: height / 2
                                color: btnTap.pressed ? Theme.pressFill : btnHover.hovered && dockRoot.currentPage !== btn.modelData.id ? Theme.hoverFill : "transparent"
                                border.width: btn.activeFocus ? 1.5 : 0
                                border.color: Theme.focusRing
                            }
                            VIcon {
                                anchors.centerIn: parent
                                name: btn.modelData.icon
                                strokeWidth: 1.7
                                color: Theme.t1
                                opacity: dockRoot.currentPage === btn.modelData.id || tip.shown ? 1.0 : 0.45
                                Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animNormal) } }
                            }
                            DockTip {
                                id: tip
                                shown: btnHover.hovered || btn.activeFocus || dockRoot.forcedTip === btn.modelData.id
                                text: btn.modelData.tip
                            }
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

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.strokeStrong }

            Item {
                id: pillToggleBtn
                objectName: "dock-pill-toggle"
                visible: dockRoot.pillToggleVisible
                implicitWidth: visible ? 32 : 0
                implicitHeight: 30
                activeFocusOnTab: dockRoot.opened && visible
                Accessible.role: Accessible.Button
                Accessible.name: dockRoot.pillHidden ? qsTr("显示播放条") : qsTr("隐藏播放条")
                Accessible.onPressAction: dockRoot.requestTogglePill()
                Keys.onReturnPressed: dockRoot.requestTogglePill()
                Keys.onEnterPressed: dockRoot.requestTogglePill()
                Keys.onSpacePressed: dockRoot.requestTogglePill()
                scale: pillTap.pressed ? 0.86 : 1
                Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: pillTap.pressed ? Theme.pressFill : pillHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: pillToggleBtn.activeFocus ? 1.5 : 0
                    border.color: Theme.focusRing
                    Behavior on color { ColorAnimation { duration: Theme.d(Theme.animNormal) } }
                }
                VIcon {
                    anchors.centerIn: parent
                    name: dockRoot.pillHidden ? "eye" : "eyeoff"
                    color: Theme.t1
                    opacity: pillHover.hovered || pillToggleBtn.activeFocus ? 1.0 : 0.45
                }
                DockTip {
                    shown: pillHover.hovered || pillToggleBtn.activeFocus
                    text: dockRoot.pillHidden ? qsTr("显示播放条") : qsTr("隐藏播放条")
                }
                HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: pillTap; onTapped: dockRoot.requestTogglePill() }
            }

            Item {
                implicitWidth: 19
                implicitHeight: 30
                VDot { anchors.centerIn: parent; off: !veyra.running; warn: veyra.captureRecovering }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.strokeStrong }

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
                    implicitWidth: 30
                    implicitHeight: 30
                    activeFocusOnTab: dockRoot.opened
                    Accessible.role: Accessible.Button
                    Accessible.name: wbtn.modelData.tip
                    Accessible.onPressAction: {
                        if (wbtn.modelData.id === "min") dockRoot.requestMinimize()
                        else if (wbtn.modelData.id === "max") dockRoot.requestMaximize()
                        else dockRoot.requestClose()
                    }
                    Keys.onReturnPressed: Accessible.onPressAction()
                    Keys.onEnterPressed: Accessible.onPressAction()
                    Keys.onSpacePressed: Accessible.onPressAction()
                    scale: wtap.pressed ? 0.86 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: whover.hovered
                            ? (wbtn.modelData.id === "close" ? Theme.errGlow : Theme.hoverFill)
                            : (wtap.pressed ? Theme.pressFill : "transparent")
                        border.width: wbtn.activeFocus ? 1.5 : 0
                        border.color: Theme.focusRing
                        Behavior on color { ColorAnimation { duration: Theme.d(Theme.animNormal) } }
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: wbtn.modelData.icon
                        size: 13
                        color: whover.hovered && wbtn.modelData.id === "close" ? Theme.err : Theme.t1
                        opacity: whover.hovered || wbtn.activeFocus ? 1.0 : 0.45
                        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animNormal) } }
                    }
                    DockTip {
                        shown: whover.hovered || wbtn.activeFocus
                        text: wbtn.modelData.tip
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
