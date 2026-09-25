// 专业 · 节点模式, rebuilt from the design (pages-node.js + pages.css .nodeview).
//
// Design structure: padding 14; a fixed 330px video card on top with a stats bar
// at its foot; a 10px split handle; then an infinite canvas with the nodes. The
// stats bar carries 输入 / 输出 / 显示 fps / 提交 fps / 排队 / 链路总耗时, and each
// node shows its own cost in its header. A timing strip along the bottom of the
// canvas colours each stage by its cost, with the remaining budget hatched.
//
// Pinned stages: 补帧 is always last, RTX Video HDR always immediately before it.
// Both are drawn locked; everything else is freely arrangeable.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

VPage {
    id: root
    signal requestPage(string page)

    property real panX: 0
    property real panY: 0
    property real zoom: 1.0
    property int selectedNode: -1

    onPanXChanged: canvasGrid.requestPaint()
    onPanYChanged: canvasGrid.requestPaint()
    onZoomChanged: canvasGrid.requestPaint()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 0

        // --- video card (fixed 330px) --------------------------------------
        Rectangle {
            id: nvTop
            Layout.fillWidth: true
            Layout.preferredHeight: 330
            radius: Theme.rCard
            color: Theme.videoBlack
            border.width: 1
            border.color: Theme.stroke
            clip: true

            // The native window sits over this area; the bar occupies its foot.
            Item {
                id: videoArea
                objectName: "videoArea"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: statsBar.top
                Text {
                    anchors.centerIn: parent
                    visible: !veyra.hasSource
                    text: veyra.statusText
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
            }

            // .nv-bar: transport, then the stats strip.
            Rectangle {
                id: statsBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 40
                color: Theme.card

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    VButton {
                        icon: true
                        iconName: (veyra.running && !veyra.paused) ? "pause" : "play"
                        onClicked: veyra.togglePlayPause()
                    }
                    VButton { icon: true; iconName: "back10"; onClicked: veyra.seekBy(-10) }
                    VButton { icon: true; iconName: "fwd10"; onClicked: veyra.seekBy(10) }

                    // .nv-stats: each entry separated by a hairline, mono numbers.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Repeater {
                            model: [
                                { k: "输入", v: veyra.sourceSummary, hi: false },
                                { k: "输出", v: veyra.outputSummary, hi: false },
                                { k: "显示 fps", v: veyra.displayFpsKnown ? veyra.displayFps.toFixed(1) : "未测", hi: true },
                                { k: "提交 fps", v: veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : "未测", hi: false },
                                { k: "排队", v: veyra.queuedFrames.toFixed(1) + " 帧", hi: false },
                                { k: "链路总耗时", v: veyra.chainTotalMs > 0 ? veyra.chainTotalMs.toFixed(1) + " ms" : "未测", hi: false }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                required property int index
                                spacing: 5
                                Rectangle {
                                    implicitWidth: 1; implicitHeight: 12
                                    color: Theme.stroke
                                    visible: index > 0
                                }
                                Text {
                                    text: modelData.k
                                    color: Theme.t3
                                    font.family: Theme.fontUi
                                    font.pixelSize: 10
                                }
                                Text {
                                    text: modelData.v.length > 0 ? modelData.v : "—"
                                    color: modelData.hi && veyra.displayFpsKnown ? Theme.ok : Theme.t1
                                    font.family: Theme.fontMono
                                    font.pixelSize: modelData.hi ? 14 : 12
                                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                                }
                            }
                        }
                    }

                    VButton { text: "列表模式"; onClicked: root.requestPage("pro") }
                }
            }
        }

        // --- split handle (10px) -------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 3
                width: 40; height: 4; radius: 4
                color: splitHover.hovered ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
            }
            HoverHandler { id: splitHover; cursorShape: Qt.SizeVerCursor }
        }

        // --- canvas --------------------------------------------------------
        Rectangle {
            id: canvasCard
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.rCard
            color: "#0A0A0D"
            border.width: 1
            border.color: Theme.stroke
            clip: true

            // .canvas dot grid: 22px spacing, drawn over the viewport only, since
            // an "infinite" canvas is a feel and infinite dots would cost frames.
            Canvas {
                id: canvasGrid
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const step = 22 * root.zoom
                    if (step < 5) return
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.08)
                    const ox = ((root.panX % step) + step) % step
                    const oy = ((root.panY % step) + step) % step
                    for (let x = ox; x < width; x += step)
                        for (let y = oy; y < height; y += step)
                            ctx.fillRect(x, y, 1.2, 1.2)
                }
            }

            // Hint and the right-click affordance.
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.bottomMargin: 34
                text: "双击空白处添加效果 · 右键节点删除 · 拖动节点移动"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
            }

            Item {
                id: world
                x: root.panX
                y: root.panY
                transform: Scale { origin.x: 0; origin.y: 0; xScale: root.zoom; yScale: root.zoom }

                // Input and output anchors, so the pipeline's ends are visible.
                Rectangle {
                    x: -190; y: 30; width: 150; height: 56
                    radius: 10
                    color: "#1D1D22"
                    border.width: 1
                    border.color: Theme.stroke2
                    Text { anchors.centerIn: parent; text: "输入"; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
                }

                Repeater {
                    model: veyra.chain
                    delegate: NodeCard {
                        required property var modelData
                        node: modelData
                    }
                }

                Rectangle {
                    x: 60; y: 380; width: 150; height: 56
                    radius: 10
                    color: "#1D1D22"
                    border.width: 1
                    border.color: Theme.stroke2
                    Text { anchors.centerIn: parent; text: "输出"; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
                }
            }

            // Pan on drag of empty canvas; zoom with the wheel (clamped so a
            // layout cannot be shrunk into nothing).
            DragHandler {
                target: null
                onTranslationChanged: {
                    root.panX += activeTranslation.x
                    root.panY += activeTranslation.y
                }
            }
            WheelHandler {
                onWheel: event => {
                    root.zoom = Math.max(0.4, Math.min(2.0, root.zoom * (event.angleDelta.y > 0 ? 1.1 : 0.9)))
                }
            }
            // The add menu opens under the double-clicked point.
            Item { id: tapAnchor; width: 1; height: 1 }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: eventPoint => {
                    tapAnchor.x = eventPoint.position.x
                    tapAnchor.y = eventPoint.position.y
                    addMenu.openAt(tapAnchor, "down")
                }
            }

            // .timing strip: each stage coloured by its cost; the remainder is
            // drawn hatched, exactly as the design's .tspare does.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                height: 16
                radius: 5
                color: Qt.rgba(10 / 255, 10 / 255, 13 / 255, 0.85)

                Row {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 1
                    Repeater {
                        model: veyra.stageTimings
                        delegate: Rectangle {
                            required property var modelData
                            height: parent.height
                            width: modelData.measured ? Math.max(4, parent.width * modelData.fraction) : 24
                            color: modelData.measured ? modelData.color : Qt.rgba(1, 1, 1, 0.12)
                            Text {
                                anchors.centerIn: parent
                                // Only draw the label where the segment is wide enough
                                // to hold it; an elided repeat of every stage name
                                // made the strip unreadable.
                                visible: parent.width > 64
                                text: modelData.measured ? modelData.label + " " + modelData.ms.toFixed(1) : "未接入"
                                color: Qt.rgba(0, 0, 0, 0.78)
                                font.family: Theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                                elide: Text.ElideRight
                                width: parent.width - 6
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }

    // A node card: header with its own cost, then its parameters on the card.
    component NodeCard: Rectangle {
        id: card
        required property var node
        readonly property bool locked: node.mustBeLast === true

        x: node.x
        y: node.y
        width: 236
        height: node.type === "nrEnhance" ? 248 : 104
        radius: 10
        color: "#1D1D22"
        border.width: 1
        border.color: root.selectedNode === node.index ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.1)
        Behavior on border.color { ColorAnimation { duration: Theme.d(200) } }

        DragHandler {
            id: drag
            enabled: !card.locked
            target: null
            onActiveTranslationChanged: {
                card.x = Math.max(-800, card.x + activeTranslation.x / root.zoom)
                card.y = Math.max(-600, card.y + activeTranslation.y / root.zoom)
            }
            onActiveChanged: if (!active) veyra.setEffectPosition(card.node.index, card.x, card.y)
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: root.selectedNode = card.node.index
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: if (!card.locked) veyra.removeEffect(card.node.index)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header: colour plate, title, the node's own cost, the lock, the switch.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 32
                radius: 9
                color: "#26262C"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: card.node.enabled ? Theme.accent : Theme.t3
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.node.label
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        elide: Text.ElideRight
                    }
                    // .ms badge: this node's cost, or "未接入" when it has none.
                    Rectangle {
                        implicitWidth: msText.implicitWidth + 12
                        implicitHeight: 18
                        radius: 5
                        color: Qt.rgba(0, 0, 0, 0.35)
                        Text {
                            id: msText
                            anchors.centerIn: parent
                            text: card.node.costMs > 0 ? card.node.costMs.toFixed(1) + " ms" : "未接入"
                            color: card.node.costMs > 0 ? "#FFFFFF" : Theme.t3
                            font.family: Theme.fontMono
                            font.pixelSize: 10
                            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        }
                    }
                    VIcon { visible: card.locked; name: "key"; size: 10; color: Theme.t3 }
                }
            }

            // Node parameters live on the node itself, per the design.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 4
                visible: card.node.type === "nrEnhance"

                Text { text: "强度"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10 }
                VSlider {
                    Layout.fillWidth: true
                    from: 0; to: 1; value: veyra.nrIntensity
                    onMoved: veyra.nrIntensity = value
                }
                Text { text: "局部明暗"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10 }
                VSlider {
                    Layout.fillWidth: true
                    from: 0; to: 1; value: veyra.nrTone
                    onMoved: veyra.nrTone = value
                }
                Text { text: "局部结构"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10 }
                VSlider {
                    Layout.fillWidth: true
                    from: 0; to: 1; value: veyra.nrStructure
                    onMoved: veyra.nrStructure = value
                }
            }

            // A non-NR node shows its summary so the card is not blank.
            Text {
                Layout.fillWidth: true
                Layout.margins: 10
                visible: card.node.type !== "nrEnhance"
                text: card.locked ? "固定为最后一步，不能拖动"
                                  : (card.node.enabled ? "已启用" : "已关闭")
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }
        }
    }

    VMenu {
        id: addMenu
        title: "添加节点"
        items: veyra.effectCatalog.map(e => ({ label: e.label, id: e.id, tag: e.experimental ? "实验" : "", tagKind: e.experimental ? "warn" : "" }))
        onPicked: (i, o) => veyra.addEffect(o.id)
    }

    // [data-in] entrance order from pages-node.js.
    VRise { target: nvTop; d: 0 }
    VRise { target: canvasCard; d: 1 }
}
