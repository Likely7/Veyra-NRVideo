// The number beside a slider. A click turns it into a text field in place
// (field request 2026-10-05: "所有的调整参数的滑条，要可以点击数字直接输入"):
// Enter or leaving the field applies the typed value through the slider, so the
// range clamp, inputScale and the owner's onMoved are the slider's own; Esc cancels.
import QtQuick

Item {
    id: valueText
    // The VSlider this number belongs to; without one the text is plain.
    property Item slider: null
    property string text: ""
    property color color: Theme.t1
    property int pixelSize: Theme.fsSmall
    property int horizontalAlignment: Text.AlignRight
    readonly property bool editable: slider !== null && slider.editable !== false && slider.enabledControl !== false
    readonly property bool editing: editor.visible
    implicitWidth: label.implicitWidth
    implicitHeight: Math.max(label.implicitHeight, 20)

    function openEditor() {
        if (!editable) return
        input.text = Number((slider.value * slider.inputScale).toFixed(slider.inputDecimals)).toString()
        editor.visible = true
        input.forceActiveFocus()
        input.selectAll()
    }
    function closeEditor(apply) {
        if (!editor.visible) return
        editor.visible = false
        if (apply) {
            const raw = input.text.trim().replace(",", ".")
            const typed = Number(raw)
            if (raw.length > 0 && isFinite(typed)) {
                const lo = Math.min(slider.from, slider.to), hi = Math.max(slider.from, slider.to)
                slider.commitValue(Math.max(lo, Math.min(hi, typed / slider.inputScale)))
            }
        }
    }

    Text {
        id: label
        objectName: "vslider-value"
        anchors.fill: parent
        visible: !editor.visible
        text: valueText.text
        color: valueText.color
        font.family: Theme.fontMono
        font.pixelSize: valueText.pixelSize
        horizontalAlignment: valueText.horizontalAlignment
        verticalAlignment: Text.AlignVCenter
        font.underline: valueText.editable && hover.hovered
    }
    HoverHandler { id: hover; enabled: valueText.editable; cursorShape: Qt.IBeamCursor }
    TapHandler {
        enabled: valueText.editable
        gesturePolicy: TapHandler.WithinBounds
        onTapped: valueText.openEditor()
    }
    Rectangle {
        id: editor
        objectName: "vslider-value-editor"
        visible: false
        z: 10
        // At least wide enough for "-250.00"; grows to the left over the slider.
        width: Math.max(valueText.width, 58)
        height: 24
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        radius: 6
        color: Theme.dialog
        border.width: 1
        border.color: Theme.accent
        TextInput {
            id: input
            objectName: "vslider-value-input"
            anchors.fill: parent
            anchors.leftMargin: 6; anchors.rightMargin: 6
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignRight
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSmall
            selectByMouse: true
            clip: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            Keys.onReturnPressed: valueText.closeEditor(true)
            Keys.onEnterPressed: valueText.closeEditor(true)
            Keys.onEscapePressed: valueText.closeEditor(false)
            // Typing must not reach the window's seek / volume shortcuts.
            Keys.onShortcutOverride: event => event.accepted = true
            onActiveFocusChanged: if (!activeFocus && editor.visible) valueText.closeEditor(true)
        }
    }
}
