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
    property bool switchAvailable: true
    property string switchObjectName: ""
    property bool on: true
    property bool open: false
    // [{ label, count, open }] — nested groups, as the design draws them.
    property var groups: []
    signal toggled(bool on)
    signal headerClicked()
    // A brief accent pulse on the border: "here it is" after 处理顺序 locates a card.
    function flash() { flashAnim.restart() }
    property real flashLevel: 0
    SequentialAnimation {
        id: flashAnim
        NumberAnimation { target: acc; property: "flashLevel"; to: 1; duration: Theme.d(160) }
        // The hold is information, not motion: it stays with reduced motion
        // (only the fades collapse to instant).
        PauseAnimation { duration: 700 }
        NumberAnimation { target: acc; property: "flashLevel"; to: 0; duration: Theme.d(520) }
    }
    // M30 .acc.new / .acc.bye: a card that was just added pops in (scale .85,
    // y -8 -> rest over .6s --spring); a removed one shrinks to .9 and fades
    // over .3s --out, then `done` runs the actual removal.
    property bool popIn: false
    property real motionS: 1
    property real motionDy: 0
    transform: [
        Scale { origin.x: acc.width / 2; origin.y: acc.height / 2; xScale: acc.motionS; yScale: acc.motionS },
        Translate { y: acc.motionDy }
    ]
    Component.onCompleted: if (popIn && Theme.d(600) > 0) { motionS = 0.85; motionDy = -8; opacity = 0; popAnim.start() }
    ParallelAnimation {
        id: popAnim
        NumberAnimation { target: acc; property: "motionS"; to: 1; duration: Theme.d(600); easing.bezierCurve: Theme.spring }
        NumberAnimation { target: acc; property: "motionDy"; to: 0; duration: Theme.d(600); easing.bezierCurve: Theme.spring }
        NumberAnimation { target: acc; property: "opacity"; to: 1; duration: Theme.d(300) }
    }
    property var byeDone: null
    function bye(done) {
        byeDone = done
        if (Theme.d(300) <= 0) { const f = byeDone; byeDone = null; if (f) f(); return }
        byeAnim.restart()
    }
    ParallelAnimation {
        id: byeAnim
        NumberAnimation { target: acc; property: "motionS"; to: 0.9; duration: Theme.d(300); easing.bezierCurve: Theme.easeOut }
        NumberAnimation { target: acc; property: "opacity"; to: 0; duration: Theme.d(300); easing.bezierCurve: Theme.easeOut }
        onFinished: { const f = acc.byeDone; acc.byeDone = null; if (f) f() }
    }
    Rectangle {
        anchors.fill: parent
        z: 10
        radius: acc.radius
        color: Qt.rgba(1, 138 / 255, 61 / 255, 0.08 * acc.flashLevel)
        border.width: 2
        border.color: Theme.accent
        opacity: acc.flashLevel
        visible: acc.flashLevel > 0
    }
    // Caller-supplied rows land in the body. Declared here, on the root, because
    // a default property belongs to the component it is declared in.
    default property alias content: body.data
    // Optional actions stay in the header; ordinary accordions remain unchanged.
    property alias headerActions: headerTools.data
    property alias headerLeadingActions: leadingTools.data
    property bool compactHeader: false
    property bool bypassed: false

    readonly property int headerHeight: compactHeader ? 42 : 48
    implicitHeight: headerHeight + (open ? body.implicitHeight + 8 : 0)
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: open ? Theme.stroke2 : Theme.stroke
    Behavior on border.color { ColorAnimation { duration: Theme.d(250) } }
    Behavior on implicitHeight { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.springSoft } }
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
                spacing: acc.compactHeader ? 8 : 9

                RowLayout {
                    id: leadingTools
                    spacing: 0
                    visible: children.length > 0
                }

                // .sico: a 26px plate tinted by the effect's own hue.
                Rectangle {
                    visible: !leadingTools.visible
                    implicitWidth: 26
                    implicitHeight: 26
                    radius: 8
                    color: Qt.rgba(acc.hue.r, acc.hue.g, acc.hue.b, 0.16)
                    VIcon {
                        anchors.centerIn: parent
                        name: acc.glyph
                        size: 14
                        color: acc.hue
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: acc.title
                        color: acc.bypassed ? Theme.t3 : Theme.t1
                        font.strikeout: acc.bypassed
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
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

                RowLayout {
                    id: headerTools
                    spacing: 2
                    visible: children.length > 0
                }

                VSwitch {
                    objectName: acc.switchObjectName
                    visible: acc.enabledSwitch
                    enabled: acc.switchAvailable || acc.on
                    opacity: enabled ? 1 : 0.4
                    checked: acc.on
                    onToggled: checked => acc.toggled(checked)
                }

                // .chev rotates 90 degrees when the card is open.
                VIcon {
                    name: "right"
                    color: Theme.t3
                    rotation: acc.open ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                }
            }

            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: point => {
                for (const tools of [leadingTools, headerTools]) {
                    const p = tools.mapFromItem(parent, point.position.x, point.position.y)
                    if (tools.visible && p.x >= 0 && p.x <= tools.width
                            && p.y >= 0 && p.y <= tools.height) return
                }
                acc.open = !acc.open; acc.headerClicked()
            } }
        }

        // --- body --------------------------------------------------------
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.leftMargin: 12 + (acc.compactHeader ? acc.border.width : 0)
            Layout.rightMargin: 12 + (acc.compactHeader ? acc.border.width : 0)
            Layout.bottomMargin: 10
            spacing: 2
            visible: acc.open || acc.height > acc.headerHeight
            // .acc-b .inner: opacity .2s and translateY(-6px) -> 0 over .45s --spring,
            // delayed .06s when opening; closing starts at once.
            property real dy: acc.open ? 0 : -6
            opacity: acc.open ? (acc.bypassed ? 0.45 : 1) : 0
            transform: Translate { y: body.dy }
            Behavior on opacity { SequentialAnimation {
                PauseAnimation { duration: acc.open ? Theme.d(60) : 0 }
                NumberAnimation { duration: Theme.d(200) } } }
            Behavior on dy { SequentialAnimation {
                PauseAnimation { duration: acc.open ? Theme.d(60) : 0 }
                NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } } }

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
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
    }
}
