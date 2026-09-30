// .pop / .opt — the design's popover menu (core.js app.menu, app.css .pop).
//
// Items: [{ label, note, icon, checked, disabled, tag, sep, head }] like the design's
// list; `picked(index, item)` fires 130 ms after the click, once the check has moved,
// and the menu closes (core.js: setTimeout(() => { closeMenu(); onPick(o) }, 130)).
// Opening (M14): opacity .15s, scale .9 -> 1 and translate 0 8px -> 0 over .45s
// --spring, from the anchor's centre on the edge nearest the anchor.
// The check (M15): opacity .15s, scale .4 -> 1 over .4s --spring.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Popup {
    id: pop
    property string title: ""
    property var items: []
    property Item anchorItem: null
    property string placement: "up"
    property bool above: false
    // Optional local-window bottom boundary for upward menus. The fullscreen
    // cinema bar uses this to keep a popover clear of the 92px playback pill.
    property real aboveLimit: -1
    signal picked(int index, var item)

    // The design's local copy of the checked flags, so the tick moves on click
    // before the owner's model catches up.
    property var checks: []
    onItemsChanged: checks = items.map(o => o.checked === true)

    function positionAtAnchor() {
        if (!anchorItem) return
        const win = anchorItem.Window.contentItem
        const r = anchorItem.mapToItem(win, 0, 0)
        const ph = height
        const pw = width
        // "at": a context menu whose top-left corner sits at the pointer (the
        // anchor is a 1px item moved there), flipped to stay inside the window.
        if (placement === "at") {
            const ax = r.x + 2 + pw > win.width - 8 ? r.x - pw - 2 : r.x + 2
            const ay = r.y + 2 + ph > win.height - 8 ? r.y - ph - 2 : r.y + 2
            const cx = Math.max(8, Math.min(ax, win.width - pw - 8)), cy = Math.max(8, Math.min(ay, win.height - ph - 8))
            above = cy < r.y
            originX = r.x - cx
            const q = parent.mapFromItem(win, cx, cy)
            x = q.x; y = q.y
            return
        }
        let px = Math.max(8, Math.min(r.x + anchorItem.width / 2 - pw / 2, win.width - pw - 8))
        let py = placement === "down" ? r.y + anchorItem.height + 8 : r.y - ph - 8
        if (placement !== "down" && aboveLimit >= 0)
            py = Math.min(py, aboveLimit - ph)
        if (py < 8)
            py = placement !== "down" && aboveLimit >= 0 ? 8 : r.y + anchorItem.height + 8
        if (py + ph > win.height - 8) py = Math.max(8, r.y - ph - 8)
        above = py < r.y
        originX = r.x + anchorItem.width / 2 - px
        const p = parent.mapFromItem(win, px, py)
        x = p.x; y = p.y
    }
    // place: "up" (the design's default), "down" (selects) or "at" (context
    // menus, at the pointer).
    function openAt(anchor, place) {
        anchorItem = anchor
        placement = place
        checks = items.map(o => o.checked === true)
        width = place === "at" ? 220 : Math.max(220, anchor.width)
        positionAtAnchor()
        open()
        Qt.callLater(positionAtAnchor)
    }
    onHeightChanged: if (visible && anchorItem) positionAtAnchor()
    property real originX: width / 2
    // For the motion probe.
    readonly property real motionScale: motion.s

    parent: Overlay.overlay
    padding: 0
    modal: false
    // Takes focus so Esc closes it (CloseOnEscape needs the popup focused).
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    implicitHeight: Math.min(420, col.implicitHeight + 12)
    background: null

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.d(150) }
            NumberAnimation { target: motion; property: "s"; from: 0.9; to: 1; duration: Theme.d(450); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: motion; property: "dy"; from: 8; to: 0; duration: Theme.d(450); easing.bezierCurve: Theme.spring }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.d(150) }
            NumberAnimation { target: motion; property: "s"; to: 0.9; duration: Theme.d(450); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: motion; property: "dy"; to: 8; duration: Theme.d(450); easing.bezierCurve: Theme.spring }
        }
    }
    QtObject { id: motion; property real s: 1; property real dy: 0 }

    // Popup itself is not an Item, so one content item carries the shadow, the panel
    // and the list, and the scale/translate motion with them.
    contentItem: Item {
        // Cut out of the native video window (main.cpp syncVideoCovers).
        objectName: "videoCover"
        property real coverRadius: 14
        transform: [
            Scale {
                origin.x: pop.originX
                origin.y: pop.above ? pop.height : 0
                xScale: motion.s
                yScale: motion.s
            },
            Translate { y: motion.dy }
        ]
        // box-shadow: 0 24px 50px -12px rgba(0,0,0,.8): the -12px spread insets the
        // shadow's shape, the offset and blur come from the effect.
        Rectangle {
            id: shadowSrc
            anchors.fill: parent
            anchors.margins: 12
            radius: 14
            color: "black"
            visible: false
        }
        MultiEffect {
            source: shadowSrc
            anchors.fill: shadowSrc
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.8)
            shadowBlur: 1.0
            blurMax: 50
            shadowVerticalOffset: 24
            autoPaddingEnabled: true
        }
        Rectangle {
            anchors.fill: parent
            radius: 14
            color: Theme.popover
            border.width: 1
            border.color: Theme.stroke2
        }
        Flickable {
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            contentHeight: col.implicitHeight
            implicitHeight: col.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: VScrollBar { }
            ColumnLayout {
                id: col
                width: parent.width
                spacing: 0
                // .pop h6
                Text {
                    visible: pop.title.length > 0
                    Layout.margins: 6
                    Layout.leftMargin: 10
                    text: pop.title
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    font.letterSpacing: 0.44
                }
                Repeater {
                    model: pop.items
                    delegate: Loader {
                        id: row
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        sourceComponent: modelData.sep ? sepC : modelData.head ? headC : optC
                        Component {
                            id: sepC
                            Item {
                                implicitHeight: 9
                                Rectangle { x: 6; width: parent.width - 12; y: 4; height: 1; color: Theme.stroke }
                            }
                        }
                        Component {
                            id: headC
                            Text {
                                topPadding: 6; bottomPadding: 6; leftPadding: 10
                                text: row.modelData.head
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }
                        }
                        Component {
                            id: optC
                            // .opt: padding 8px 10px, radius 9, hover rgba(255,255,255,.06).
                            Rectangle {
                                implicitHeight: optRow.implicitHeight + 16
                                radius: 9
                                opacity: row.modelData.disabled ? 0.4 : 1
                                color: optHover.hovered && !row.modelData.disabled ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                                RowLayout {
                                    id: optRow
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 10
                                    VIcon { visible: !!row.modelData.icon; name: row.modelData.icon || ""; color: Theme.t1 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.label
                                            color: Theme.t1
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsBody
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: !!row.modelData.note
                                            Layout.fillWidth: true
                                            text: row.modelData.note || ""
                                            color: Theme.t3
                                            font.family: Theme.fontUi
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                    VTag { visible: !!row.modelData.tag; text: row.modelData.tag || ""; kind: row.modelData.tagKind || "" }
                                    // .chk: margin-left auto, accent, .4 -> 1 on the spring.
                                    VIcon {
                                        name: "check"
                                        color: Theme.accent
                                        readonly property bool on: pop.checks[row.index] === true
                                        opacity: on ? 1 : 0
                                        scale: on ? 1 : 0.4
                                        Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                                    }
                                }
                                HoverHandler { id: optHover; enabled: !row.modelData.disabled; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    enabled: !row.modelData.disabled
                                    gesturePolicy: TapHandler.WithinBounds
                                    onTapped: {
                                        pop.checks = pop.items.map((o, i) => i === row.index)
                                        pickTimer.index = row.index
                                        pickTimer.restart()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Timer {
        id: pickTimer
        property int index: -1
        interval: 130
        onTriggered: { pop.close(); pop.picked(index, pop.items[index]) }
    }
}
