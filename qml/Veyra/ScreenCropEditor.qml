import QtQuick
import QtQuick.Layouts

Item {
    id: editor
    anchors.fill: parent
    visible: editing
    property bool editing: false
    property rect hostRect
    property var draft: ({left: 0, top: 0, right: 0, bottom: 0})
    readonly property bool ready: veyra.hasSource && !veyra.openingSource && veyra.sourceKind === "screen" &&
                                  veyra.sourceWidth >= 16 && veyra.sourceHeight >= 16
    readonly property real scaleFactor: ready ? Math.min(hostRect.width / veyra.sourceWidth, hostRect.height / veyra.sourceHeight) : 0
    signal cancelled()

    function begin(options) {
        draft = {left: options.left, top: options.top, right: options.right, bottom: options.bottom}
        editing = true
    }
    function clamp(n, high) { return Math.round(Math.min(Math.max(0, high), Math.max(0, n))) }
    function margins() {
        const w = Math.max(0, veyra.sourceWidth - 16)
        const h = Math.max(0, veyra.sourceHeight - 16)
        const left = clamp(draft.left, w)
        const top = clamp(draft.top, h)
        return {left: left, top: top, right: clamp(draft.right, w - left), bottom: clamp(draft.bottom, h - top)}
    }
    readonly property var cut: margins()

    Item {
        id: picture
        visible: editor.ready && editor.scaleFactor > 0
        width: veyra.sourceWidth * editor.scaleFactor
        height: veyra.sourceHeight * editor.scaleFactor
        x: editor.hostRect.x + (editor.hostRect.width - width) / 2
        y: editor.hostRect.y + (editor.hostRect.height - height) / 2
        readonly property real x0: editor.cut.left * editor.scaleFactor
        readonly property real y0: editor.cut.top * editor.scaleFactor
        readonly property real x1: width - editor.cut.right * editor.scaleFactor
        readonly property real y1: height - editor.cut.bottom * editor.scaleFactor
        Repeater {
            model: [
                Qt.rect(picture.x0, picture.y0, 3, picture.y1 - picture.y0),
                Qt.rect(picture.x0, picture.y0, picture.x1 - picture.x0, 3),
                Qt.rect(Math.max(0, picture.x1 - 3), picture.y0, 3, picture.y1 - picture.y0),
                Qt.rect(picture.x0, Math.max(0, picture.y1 - 3), picture.x1 - picture.x0, 3)
            ]
            delegate: Rectangle {
                required property rect modelData
                readonly property bool videoCover: true
                x: modelData.x; y: modelData.y; width: modelData.width; height: modelData.height
                color: Theme.accent
            }
        }
        MouseArea {
            objectName: "screen-crop-drag"
            anchors.fill: parent
            preventStealing: true
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
            property point start
            property var original
            property int horizontal: 0
            property int vertical: 0
            onPressed: mouse => {
                const l = Math.abs(mouse.x - picture.x0), r = Math.abs(mouse.x - picture.x1)
                const t = Math.abs(mouse.y - picture.y0), b = Math.abs(mouse.y - picture.y1)
                horizontal = Math.min(l, r) <= 16 ? (l <= r ? -1 : 1) : 0
                vertical = Math.min(t, b) <= 16 ? (t <= b ? -1 : 1) : 0
                if (!horizontal && !vertical &&
                    (mouse.x < picture.x0 || mouse.x > picture.x1 || mouse.y < picture.y0 || mouse.y > picture.y1)) {
                    mouse.accepted = false
                    return
                }
                start = Qt.point(mouse.x, mouse.y)
                original = editor.cut
            }
            onPositionChanged: mouse => {
                if (!pressed || editor.scaleFactor <= 0 || !original) return
                const dx = Math.round((mouse.x - start.x) / editor.scaleFactor)
                const dy = Math.round((mouse.y - start.y) / editor.scaleFactor)
                const w = veyra.sourceWidth - 16, h = veyra.sourceHeight - 16
                const m = {left: original.left, top: original.top, right: original.right, bottom: original.bottom}
                if (horizontal < 0) m.left = editor.clamp(m.left + dx, w - m.right)
                if (horizontal > 0) m.right = editor.clamp(m.right - dx, w - m.left)
                if (vertical < 0) m.top = editor.clamp(m.top + dy, h - m.bottom)
                if (vertical > 0) m.bottom = editor.clamp(m.bottom - dy, h - m.top)
                if (!horizontal && !vertical) {
                    const x = Math.min(m.right, Math.max(-m.left, dx))
                    const y = Math.min(m.bottom, Math.max(-m.top, dy))
                    m.left += x; m.right -= x; m.top += y; m.bottom -= y
                }
                editor.draft = m
            }
        }
    }

    Rectangle {
        readonly property bool videoCover: true
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: actions.implicitWidth + 20
        height: actions.implicitHeight + 14
        radius: 10
        color: Theme.card
        border.color: Theme.stroke
        RowLayout {
            id: actions
            anchors.centerIn: parent
            Text {
                text: editor.ready ? qsTr("拖动边缘或移动裁剪框 · %1 × %2")
                                         .arg(veyra.sourceWidth - editor.cut.left - editor.cut.right)
                                         .arg(veyra.sourceHeight - editor.cut.top - editor.cut.bottom) : veyra.statusText
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            VButton { text: qsTr("重置"); onClicked: editor.begin({left:0, top:0, right:0, bottom:0}) }
            VButton { text: qsTr("取消"); onClicked: { editor.editing = false; editor.cancelled() } }
            VButton {
                objectName: "screen-crop-apply"
                text: qsTr("应用裁剪")
                primary: true
                enabled: editor.ready
                onClicked: {
                    const cut = editor.cut
                    if (veyra.applyScreenCrop(cut.left, cut.top, cut.right, cut.bottom)) editor.editing = false
                }
            }
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: editor.editing
        onActivated: { editor.editing = false; editor.cancelled() }
    }
}
