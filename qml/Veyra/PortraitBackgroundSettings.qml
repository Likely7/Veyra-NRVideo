import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VGroup {
    id: panel
    required property Item controller
    readonly property var words: controller.words
    readonly property bool selectable: controller.mode === "builtin" || controller.mode === "custom"
    property bool editingLeft: true

    function chooseSide(left) {
        if (controller.mode === "custom") {
            controller.chooseCustom(left)
        } else if (controller.mode === "builtin") {
            editingLeft = left
            gallery.open()
        }
    }

    VRow {
        label: panel.words.title
        hint: panel.controller.mode === "slideshow" ? panel.words.interval : ""
        hintBelow: true
        VSeg {
            objectName: "set-side-background-mode"
            options: [{id: "black", label: panel.words.black}, {id: "builtin", label: panel.words.builtin},
                      {id: "custom", label: panel.words.custom}, {id: "slideshow", label: panel.words.slideshow}]
            current: panel.controller.mode
            onPicked: id => panel.controller.setMode(id)
        }
    }
    VRow {
        visible: panel.controller.mode !== "black"
        label: panel.words.preview
        hint: panel.selectable ? panel.words.select : ""
        Row {
            spacing: 16
            Repeater {
                model: 2
                delegate: Column {
                    id: side
                    required property int index
                    readonly property bool isLeft: index === 0
                    spacing: 3
                    Rectangle {
                        width: 42
                        height: 144
                        color: "black"
                        Image {
                            id: sample
                            anchors.fill: parent
                            anchors.margins: 2
                            source: panel.controller.settingsOpen ? panel.controller.sideUrl(side.isLeft) : ""
                            sourceSize: Qt.size(96, 346)
                            fillMode: Image.PreserveAspectCrop
                            autoTransform: true
                            asynchronous: true
                            clip: true
                            opacity: panel.controller.brightness / 100
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: panel.selectable && (sample.source.toString() === "" || sample.status === Image.Error)
                            text: sample.status === Image.Error ? "!" : "+"
                            color: Theme.t2
                            font.pixelSize: 20
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: hover.hovered && panel.selectable ? Theme.accent : Theme.stroke2
                        }
                        HoverHandler { id: hover; cursorShape: panel.selectable ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        TapHandler { enabled: panel.selectable; onTapped: panel.chooseSide(side.isLeft) }
                        activeFocusOnTab: panel.selectable
                        Keys.onSpacePressed: if (panel.selectable) panel.chooseSide(side.isLeft)
                        Keys.onReturnPressed: if (panel.selectable) panel.chooseSide(side.isLeft)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: side.isLeft ? panel.words.left : panel.words.right
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
    VRow {
        label: panel.words.brightness
        value: Math.round(panel.controller.brightness) + "%"
        VSlider {
            objectName: "set-side-background-brightness"
            width: 150
            height: 30
            from: 0
            to: 100
            stepSize: 1
            valueFromModel: true
            value: panel.controller.brightness
            defaultValue: 60
            resettable: true
            onMoved: level => panel.controller.setBrightness(level)
        }
    }

    Popup {
        id: gallery
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(540, parent ? parent.width - 24 : 540)
        height: Math.min(428, parent ? parent.height - 24 : 428)
        padding: 14
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.card; radius: Theme.rCard; border.color: Theme.stroke2 }
        contentItem: ColumnLayout {
            RowLayout {
                Layout.fillWidth: true
                VH2 { Layout.fillWidth: true; text: panel.words.builtin + " · " + (panel.editingLeft ? panel.words.left : panel.words.right) }
                VButton { icon: true; iconName: "x"; onClicked: gallery.close() }
            }
            GridView {
                id: grid
                Layout.fillHeight: true
                Layout.fillWidth: true
                cellWidth: 67
                cellHeight: 161
                clip: true
                model: gallery.visible ? panel.controller.gallery : []
                ScrollBar.vertical: VScrollBar {}
                delegate: Item {
                    required property string modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    Rectangle {
                        anchors.centerIn: parent
                        width: 46
                        height: 154
                        color: "black"
                        border.width: 2
                        border.color: panel.controller.builtIn(panel.editingLeft) === modelData ? Theme.accent : Theme.stroke2
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            source: panel.controller.galleryUrl(modelData)
                            sourceSize: Qt.size(96, 346)
                            fillMode: Image.PreserveAspectCrop
                            clip: true
                            asynchronous: true
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            panel.controller.setBuiltIn(panel.editingLeft, modelData)
                            gallery.close()
                        }
                    }
                }
            }
        }
    }
}
