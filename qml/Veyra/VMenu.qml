// .pop / .opt — the design's popover menu (core.js app.menu, app.css .pop).
//
// Items: [{ label, note, icon, checked, disabled, danger, gold, tag, sep, head }] like the design's
// list; `picked(index, item)` fires 130 ms after the click, once the check has moved,
// and the menu closes (core.js: setTimeout(() => { closeMenu(); onPick(o) }, 130)).
// Opening (M14): opacity .15s, scale .9 -> 1 and translate 0 8px -> 0 over .45s
// --spring, from the anchor's centre on the edge nearest the anchor.
// The check (M15): opacity .15s, scale .4 -> 1 over .4s --spring.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    readonly property real menuAvailableWidth: Math.max(1, (parent ? parent.width : 640) - 16)
    readonly property real measuredWidth: {
        let widest = titleMetrics.advanceWidth(title) + 44
        for (const o of items) {
            const extra = 52 + (o.icon ? 28 : 0) + (o.tag ? labelMetrics.advanceWidth(o.tag) + 30 : 0)
            widest = Math.max(widest, labelMetrics.advanceWidth(o.label || o.head || "") + extra)
        }
        return widest
    }
    width: Math.min(menuAvailableWidth, Math.max(220, anchorItem ? anchorItem.width : 0, Math.min(720, measuredWidth)))
    FontMetrics { id: labelMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium }
    FontMetrics { id: titleMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.fsEyebrow; font.weight: Font.Medium }
    signal picked(int index, var item)

    property int focusedIndex: -1
    function ensureVisible(index) {
        const item = itemRepeater.itemAt(index)
        if (!item) return
        const top = item.y
        const bottom = item.y + item.height
        if (top < menuFlick.contentY) menuFlick.contentY = top
        else if (bottom > menuFlick.contentY + menuFlick.height) menuFlick.contentY = Math.max(0, bottom - menuFlick.height)
    }
    onFocusedIndexChanged: if (focusedIndex >= 0) ensureVisible(focusedIndex)
    function isSelectable(i) {
        if (i < 0 || i >= items.length) return false
        const o = items[i]
        return o && !o.sep && !o.head && o.disabled !== true
    }
    function moveFocus(step) {
        if (items.length === 0) return
        let i = focusedIndex
        for (let n = 0; n < items.length; ++n) {
            i = (i + step + items.length) % items.length
            if (isSelectable(i)) { focusedIndex = i; return }
        }
    }
    function focusEdge(fromEnd) {
        for (let n = 0; n < items.length; ++n) {
            const i = fromEnd ? items.length - 1 - n : n
            if (isSelectable(i)) { focusedIndex = i; return }
        }
    }
    function triggerPick(index) {
        if (!isSelectable(index)) return
        pop.checks = pop.items.map((o, i) => i === index)
        pickTimer.index = index
        pickTimer.restart()
    }

    // The design's local copy of the checked flags, so the tick moves on click
    // before the owner's model catches up.
    property var checks: []
    onItemsChanged: checks = items.map(o => o.checked === true)

    function positionAtAnchor() {
        if (!anchorItem) return
        const win = anchorItem.Window.contentItem
        // The window can be gone while a queued placement still runs; without this
        // the next line throws "Cannot read property 'width' of null".
        if (!win) return
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
        // Finish the nested row/column layouts before the approved spring
        // starts. Otherwise the first option moves when the title's implicit
        // height arrives on the first render (14 -> 42 in the regression).
        contentItem.width = width - leftPadding - rightPadding
        contentItem.ensurePolished()
        col.ensurePolished()
        positionAtAnchor()
        open()
    }
    onHeightChanged: if (visible && anchorItem) positionAtAnchor()
    onWidthChanged: if (visible && anchorItem) positionAtAnchor()
    Connections {
        target: pop.parent
        function onWidthChanged() { if (pop.visible) Qt.callLater(pop.positionAtAnchor) }
        function onHeightChanged() { if (pop.visible) Qt.callLater(pop.positionAtAnchor) }
    }
    Connections {
        target: pop.anchorItem
        function onXChanged() { if (pop.visible) Qt.callLater(pop.positionAtAnchor) }
        function onYChanged() { if (pop.visible) Qt.callLater(pop.positionAtAnchor) }
    }
    property real originX: width / 2
    // For the motion probe.
    readonly property real motionScale: motion.s
    // Software drawing (the default since 2.1.1) puts every glyph on a whole
    // pixel, and a label left of its place by any fraction lands a full pixel
    // left. The spring's tail swings scale and lift past their end values and
    // back (1.043, 0.99 and 1.001 of the way at 24%, 44% and 61%), under half
    // a pixel on a normal menu, so each opening moved every label 1 px
    // sideways and back twice (screen recordings, 2026-11-06). With software
    // drawing the opening therefore stops where the spring first reaches its
    // end; GPU drawing still draws the whole spring.
    property bool holdLanding: (typeof vySoftwareUi !== "undefined" && vySoftwareUi)
        || contentItem.GraphicsInfo.api === GraphicsInfo.Software
    onAboutToShow: { motion.opening = true; motion.landed = false; focusedIndex = -1 }
    onAboutToHide: { motion.opening = false; motion.landed = false; focusedIndex = -1 }

    parent: Overlay.overlay
    padding: 0
    modal: false
    // Takes focus so Esc and arrow keys work.
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    implicitHeight: Math.min(420, Math.max(1, (parent ? parent.height : 640) - Theme.s16), col.implicitHeight + Theme.s12)
    background: null
    Accessible.role: Accessible.PopupMenu
    Accessible.name: pop.title

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
    QtObject {
        id: motion
        property real s: 1
        property real dy: 0
        // Set once the opening spring first reaches 1; holdLanding keeps it there.
        property bool opening: false
        property bool landed: false
        onSChanged: if (opening && s >= 1) landed = true
        readonly property bool held: landed && pop.holdLanding
    }

    // Popup itself is not an Item, so one content item carries the shadow, the panel
    // and the list, and the scale/translate motion with them.
    contentItem: Item {
        // Cut out of the native video window (main.cpp syncVideoCovers).
        objectName: "videoCover"
        property real coverRadius: Theme.rModal
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down) { pop.moveFocus(1); event.accepted = true }
            else if (event.key === Qt.Key_Up) { pop.moveFocus(-1); event.accepted = true }
            else if (event.key === Qt.Key_Home) { pop.focusEdge(false); event.accepted = true }
            else if (event.key === Qt.Key_End) { pop.focusEdge(true); event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                if (pop.focusedIndex >= 0) { pop.triggerPick(pop.focusedIndex); event.accepted = true }
            }
            else if (event.key === Qt.Key_Escape) { pop.close(); event.accepted = true }
        }
        transform: [
            Scale {
                origin.x: pop.originX
                origin.y: pop.above ? pop.height : 0
                xScale: motion.held ? 1 : motion.s
                yScale: motion.held ? 1 : motion.s
            },
            Translate { y: motion.held ? 0 : motion.dy }
        ]
        Rectangle {
            anchors.fill: parent
            radius: Theme.rModal
            color: Theme.popover
            border.width: 1
            border.color: Theme.strokeStrong
        }
        Flickable {
            id: menuFlick
            anchors.fill: parent
            anchors.margins: Theme.s4 + 2
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
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    Layout.margins: Theme.s4 + 2
                    Layout.leftMargin: Theme.s12
                    text: pop.title
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsEyebrow
                    font.weight: Font.Medium
                    font.variableAxes: Theme.axesMedium
                    font.letterSpacing: 0.44
                }
                Repeater {
                    id: itemRepeater
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
                                topPadding: Theme.s4 + 2; bottomPadding: Theme.s4 + 2; leftPadding: Theme.s12
                                text: row.modelData.head
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsEyebrow
                                font.weight: Font.Medium
                                font.variableAxes: Theme.axesMedium
                                wrapMode: Text.Wrap
                            }
                        }
                        Component {
                            id: optC
                            Rectangle {
                                readonly property bool isFocused: pop.focusedIndex === row.index
                                implicitHeight: optRow.implicitHeight + Theme.s16
                                radius: Theme.rCtl
                                opacity: row.modelData.disabled ? Theme.disabledOpacity : 1
                                color: optTap.pressed && !row.modelData.disabled ? Theme.pressFill
                                     : (optHover.hovered || isFocused) && !row.modelData.disabled ? Theme.hoverFill : "transparent"
                                border.width: isFocused ? 1 : 0
                                border.color: Theme.focusRing
                                Accessible.role: Accessible.MenuItem
                                Accessible.name: row.modelData.label || ""
                                Accessible.checked: pop.checks[row.index] === true
                                RowLayout {
                                    id: optRow
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: Theme.s12; anchors.rightMargin: Theme.s12
                                    spacing: Theme.s8
                                    VIcon { visible: !!row.modelData.icon; name: row.modelData.icon || ""; color: Theme.t1 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: Theme.s4
                                        Text {
                                            objectName: "vmenu-option-label"
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: row.modelData.label
                                            color: row.modelData.danger === true ? Theme.err
                                                   : row.modelData.gold === true ? Theme.warn : Theme.t1
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsBody
                                            font.weight: Font.Medium
                                            font.variableAxes: Theme.axesMedium
                                            wrapMode: Text.Wrap
                                        }
                                        Text {
                                            visible: !!row.modelData.note
                                            Layout.fillWidth: true
                                            text: row.modelData.note || ""
                                            color: Theme.t3
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsEyebrow
                                            wrapMode: Text.Wrap
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
                                        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animFast) } }
                                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                                    }
                                }
                                HoverHandler { id: optHover; enabled: !row.modelData.disabled; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    id: optTap
                                    enabled: !row.modelData.disabled
                                    gesturePolicy: TapHandler.WithinBounds
                                    onTapped: pop.triggerPick(row.index)
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
        onTriggered: { const it = pop.items[index] || {}; if (typeof veyra !== "undefined") veyra.logUi("ui-menu", (pop.title || pop.objectName || "menu") + " pick " + index + " " + (it.act || it.id || it.label || "")); pop.close(); pop.picked(index, pop.items[index]) }
    }
}
