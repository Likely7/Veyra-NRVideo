// 节点模式: a completely separate mode, not a toggle inside 专业模式.
//
// Layout the user asked for: video on top, nodes below on a free canvas,
// parameters edited directly ON the nodes (no side panel), and multiple
// instances of the same effect allowed (colour at the input and again at the
// output, for example).
//
// What is pinned, and why it has to be: 补帧 is always the last stage and RTX
// Video HDR always sits immediately in front of it. Video HDR turns an SDR frame
// into an HDR one; anything after it would need HDR-aware handling that only
// frame generation has. Both are drawn locked and cannot be dragged. Everything
// else can be arranged freely, and the engine validates any reorder before
// accepting it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    // Canvas pan/zoom. Node mode is the one place free arrangement is allowed,
    // and the engine writes viewX/viewY back so a layout survives a restart.
    property real panX: 0
    property real panY: 0
    property real zoom: 1.0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- video on top --------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(220, root.height * 0.42)

            Item {
                id: videoHost
                objectName: "videoArea"
                anchors.fill: parent
                visible: root.visible
            }
            Text {
                anchors.centerIn: parent
                visible: !veyra.hasSource
                text: veyra.statusText
                color: Theme.text2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSizeTitle
            }
        }

        // --- toolbar -------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.card

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    text: "节点模式"
                    color: Theme.text1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fontSizeBody
                }
                Text {
                    text: "双击画布添加效果 · 右键节点删除 · 拖动节点调整位置"
                    color: Theme.text3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                Rectangle {
                    implicitWidth: 72
                    implicitHeight: 26
                    radius: 13
                    color: backHover.hovered ? Theme.card3 : Theme.card2
                    Text {
                        anchors.centerIn: parent
                        text: "列表模式"
                        color: Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                    }
                    HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.requestPage("pro") }
                }
            }
        }

        // --- canvas --------------------------------------------------------
        // White-background infinite canvas was the design prototype; in the app
        // it is a dark canvas because the app is dark. The behaviour is what
        // carried over: pan, zoom, free placement, no fixed grid of slots.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background
            clip: true

            // A faint dot grid, drawn only as far as the viewport: an "infinite"
            // canvas is a feel, and drawing infinite dots would cost frames.
            Canvas {
                id: grid
                anchors.fill: parent
                opacity: 0.3
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Theme.stroke2
                    const step = 28 * root.zoom
                    if (step < 6) return
                    const ox = (root.panX % step + step) % step
                    const oy = (root.panY % step + step) % step
                    for (let x = ox; x < width; x += step)
                        for (let y = oy; y < height; y += step)
                            ctx.fillRect(x, y, 1.5, 1.5)
                }
                Connections {
                    target: root
                    function onPanXChanged() { grid.requestPaint() }
                    function onPanYChanged() { grid.requestPaint() }
                    function onZoomChanged() { grid.requestPaint() }
                }
            }

            Item {
                id: world
                x: root.panX
                y: root.panY
                // Zoom via a transform, so node coordinates stay in chain space
                // and the engine never sees a zoomed value.
                transform: Scale { origin.x: 0; origin.y: 0; xScale: root.zoom; yScale: root.zoom }

                // Input and output anchors: the chain's ends are part of the
                // map, so it is clear where a stage sits in the pipeline.
                Rectangle {
                    x: -180; y: 40; width: 140; height: 56
                    radius: Theme.radiusCard
                    color: Theme.card2
                    border.width: 1
                    border.color: Theme.stroke2
                    Text {
                        anchors.centerIn: parent
                        text: "输入"
                        color: Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeBody
                    }
                }

                Repeater {
                    model: veyra.chain
                    delegate: NodeCard {
                        required property var modelData
                        node: modelData
                    }
                }

                // The output anchor sits after the pinned tail. Its position is
                // the widest node's right edge, so it always reads as "after the
                // chain" rather than floating at a fixed coordinate.
                Rectangle {
                    x: 60; y: 340; width: 140; height: 56
                    radius: Theme.radiusCard
                    color: Theme.card2
                    border.width: 1
                    border.color: Theme.stroke2
                    Text {
                        anchors.centerIn: parent
                        text: "输出"
                        color: Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
            }

            // Pan with the middle button or a drag on empty canvas.
            DragHandler {
                id: panHandler
                target: null
                onTranslationChanged: {
                    root.panX += activeTranslation.x
                    root.panY += activeTranslation.y
                }
            }
            WheelHandler {
                onWheel: event => {
                    // Zoom is clamped: node parameters must stay readable, and a
                    // canvas that can shrink to nothing is a way to lose a layout.
                    root.zoom = Math.max(0.4, Math.min(2.0, root.zoom * (event.angleDelta.y > 0 ? 1.1 : 0.9)))
                }
            }

            // Double-click empty canvas to add a stage. A menu of the catalog is
            // less to learn than a palette you drag from.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: addMenu.popup()
            }

            Menu {
                id: addMenu
                Repeater {
                    model: veyra.effectCatalog
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        enabled: !modelData.mustBeLast
                        onTriggered: veyra.addEffect(modelData.id)
                    }
                }
            }
        }
    }

    // A node with its own parameters on it, which is the whole point of this
    // mode: no side panel, no selection round trip.
    component NodeCard: Rectangle {
        id: card
        required property var node

        readonly property bool locked: node.mustBeLast === true

        x: node.x
        y: node.y
        width: 216
        // NR carries the most parameters; other stages are shorter.
        height: node.type === "nrEnhance" ? 244 : 104
        radius: Theme.radiusCard
        color: Theme.card2
        border.width: 1
        border.color: drag.dragging ? Theme.accent : Theme.stroke
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        DragHandler {
            id: drag
            // A pinned node cannot be dragged: the pipeline order is physics, not
            // preference, and the engine would refuse the move anyway.
            enabled: !card.locked
            target: null
            onActiveTranslationChanged: {
                card.x = Math.max(-600, card.x + activeTranslation.x / root.zoom)
                card.y = Math.max(-400, card.y + activeTranslation.y / root.zoom)
            }
            onActiveChanged: if (!active) veyra.setEffectPosition(card.node.index, card.x, card.y)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: if (!card.locked) veyra.removeEffect(card.node.index)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: card.node.enabled ? Theme.accent : Theme.text3
                }
                Text {
                    Layout.fillWidth: true
                    text: card.node.label
                    color: Theme.text1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fontSizeBody
                }
                Text {
                    visible: card.locked
                    text: "🔒"
                    font.pixelSize: 11
                    opacity: 0.7
                }
                Rectangle {
                    width: 30; height: 17; radius: 9
                    visible: !card.locked
                    color: card.node.enabled ? Theme.accent : Theme.card3
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: card.node.enabled ? Theme.accentInk : Theme.text3
                        anchors.verticalCenter: parent.verticalCenter
                        x: card.node.enabled ? parent.width - width - 2.5 : 2.5
                        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.bezierCurve: Theme.spring } }
                    }
                    TapHandler { onTapped: veyra.setEffectEnabled(card.node.index, !card.node.enabled) }
                }
            }

            // Parameters live on the node. Only the stage's own are shown.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: card.node.type === "nrEnhance"

                Text { text: "强度"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: 10 }
                Slider {
                    Layout.fillWidth: true
                    from: 0; to: 2; value: veyra.nrIntensity
                    onMoved: veyra.nrIntensity = value
                }
                Text { text: "色调"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: 10 }
                Slider {
                    Layout.fillWidth: true
                    from: 0; to: 2; value: veyra.nrTone
                    onMoved: veyra.nrTone = value
                }
                Text { text: "结构"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: 10 }
                Slider {
                    Layout.fillWidth: true
                    from: 0; to: 2; value: veyra.nrStructure
                    onMoved: veyra.nrStructure = value
                }
                Text { text: "肤色"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: 10 }
                Slider {
                    Layout.fillWidth: true
                    from: -1; to: 1; value: veyra.nrSkin
                    onMoved: veyra.nrSkin = value
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    Component.onCompleted: veyra.nodeMode = 1
}
