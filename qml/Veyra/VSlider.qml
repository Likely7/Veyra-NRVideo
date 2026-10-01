import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: slider
    property real value: 0
    property real from: 0
    property real to: 1
    property bool center: false        // fill from the middle instead of the left
    property bool showCenterFill: false // Colour controls opt in; other pages keep their visuals.
    property bool enabledControl: true
    // A containing parameter widget can opt in without changing non-parameter sliders.
    property bool resettable: parent && typeof parent.sliderResetEnabled !== "undefined" && parent.sliderResetEnabled
    property real defaultValue: 0
    // Model-owned parameters must never lose their value binding on user input.
    // Standalone sliders retain their existing locally owned value behavior.
    property bool valueFromModel: parent && typeof parent.sliderValueFromModel !== "undefined" && parent.sliderValueFromModel
    property real dragValue: 0
    property Gradient trackGradient: null // Opt-in; existing effect sliders keep their track.
    signal moved(real value)
    // Live: the owner hears the value while the knob is dragged (every 40 ms)
    // and once more on release, instead of only on release.
    property bool live: true
    // True while the knob is held (tests read it; the owner may too).
    readonly property bool dragging: drag.active
    Timer {
        id: liveTimer
        interval: 40
        onTriggered: if (drag.active) slider.moved(slider.valueFromModel ? slider.dragValue : slider.value)
    }
    function commitValue(amount) {
        if (!valueFromModel) value = amount
        moved(amount)
    }

    // Typed values (field request 2026-10-01): double-click the slider, or press Enter while
    // it has focus, and type the number; Enter applies it (clamped to the range), Esc cancels.
    // inputScale converts to the unit the page shows (100 for a 0–1 volume shown as %).
    property bool editable: true
    property real inputScale: 1
    property int inputDecimals: {
        const span = Math.abs(to - from) * inputScale
        return span >= 50 ? 0 : span >= 2 ? 1 : span >= 0.2 ? 2 : 3
    }
    readonly property bool editing: editor.visible
    function openEditor() {
        if (!editable || !enabledControl) return
        editorInput.text = Number((value * inputScale).toFixed(inputDecimals)).toString()
        editor.visible = true
        editorInput.forceActiveFocus()
        editorInput.selectAll()
    }
    function closeEditor(apply) {
        if (!editor.visible) return
        editor.visible = false
        if (apply) {
            const typed = Number(editorInput.text.trim().replace(",", "."))
            if (editorInput.text.trim().length > 0 && isFinite(typed))
                commitValue(Math.max(Math.min(from, to), Math.min(Math.max(from, to), typed / inputScale)))
        }
        slider.forceActiveFocus()
    }

    // Keyboard (field request 2026-10-01): a clicked or dragged slider takes focus and the
    // arrow keys move it by the 设置 step (1 / 0.1 / 0.01), at least a thousandth of the
    // range so wide ranges still move. While focused the arrows belong to the slider, not
    // to the window's seek and volume shortcuts; Esc gives them back.
    activeFocusOnTab: enabledControl
    readonly property real keyStep: Math.max(Number(typeof veyra !== "undefined" && veyra.preferences ? (veyra.preferences.sliderKeyStep || 0.1) : 0.1), (to - from) / 1000)
    function nudge(direction) {
        const next = Math.max(from, Math.min(to, value + direction * keyStep))
        // Snap to the step grid so 0.1 steps do not drift into 0.30000000000000004.
        commitValue(Math.round(next / keyStep) * keyStep)
    }
    Keys.onShortcutOverride: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up
            || event.key === Qt.Key_Down || event.key === Qt.Key_Escape
            || (editable && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))) event.accepted = true
    }
    Keys.onPressed: event => {
        if (!enabledControl) return
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) { nudge(-1); event.accepted = true }
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) { nudge(1); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { focus = false; event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { openEditor(); event.accepted = true }
    }

    implicitHeight: 20
    implicitWidth: 160
    readonly property real displayedValue: valueFromModel && drag.active ? dragValue : value
    readonly property real frac: (to > from) ? Math.max(0, Math.min(1, (displayedValue - from) / (to - from))) : 0
    readonly property real trackWidth: track.width

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(1, parent.width - (slider.resettable ? 28 : 0))
        height: 4
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.12)
        gradient: slider.trackGradient
    }
    Rectangle {
        objectName: "vslider-fill"
        anchors.verticalCenter: track.verticalCenter
        height: 4
        radius: 9
        color: Theme.accent
        visible: (!slider.center || slider.showCenterFill) && !slider.trackGradient
        width: track.width * (slider.center ? Math.abs(slider.frac - 0.5) : slider.frac)
        x: slider.center ? track.width * Math.min(0.5, slider.frac) : 0
    }
    Rectangle {
        id: knob
        width: Theme.sliderKnob
        height: Theme.sliderKnob
        radius: width / 2
        color: "#FFFFFF"
        anchors.verticalCenter: track.verticalCenter
        x: track.width * slider.frac - width / 2
        scale: drag.active || hover.hovered ? 1.25 : 1.0
        border.width: slider.activeFocus ? 2 : 0
        border.color: Theme.accent
        Behavior on scale { NumberAnimation { duration: Theme.d(350); easing.bezierCurve: Theme.spring } }
    }
    // box-shadow: 0 2px 6px rgba(0,0,0,.5). A sibling MultiEffect, not layer.effect: Qt recreates a layer's effect item on
    // a screen DPI change while it walks the parent's children, and the walk then touched
    // the deleted item (crash moving the window to a 200 % monitor, field 2026-10-01).
    MultiEffect {
        source: knob
        anchors.fill: knob
        scale: knob.scale
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.5)
        shadowVerticalOffset: 2
        shadowBlur: 0.5
        blurMax: 12
        autoPaddingEnabled: true
    }
    Item {
    id: trackInput
    width: track.width
    height: parent.height
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor; enabled: slider.enabledControl }
    DragHandler {
        id: drag
        target: null
        enabled: slider.enabledControl
        // activeTranslation is measured from the press, so it adds to where the knob
        // was then, not to where it is now.
        property real startX: 0
        onActiveTranslationChanged: {
            const x = Math.max(0, Math.min(track.width, startX + activeTranslation.x))
            slider.dragValue = slider.from + (x / track.width) * (slider.to - slider.from)
            if (!slider.valueFromModel) slider.value = slider.dragValue
            if (slider.live && !liveTimer.running) liveTimer.start()
        }
        onActiveChanged: {
            if (active) {
                slider.forceActiveFocus()
                slider.dragValue = slider.value
                startX = track.width * slider.frac
            } else { liveTimer.stop(); slider.commitValue(slider.valueFromModel ? slider.dragValue : slider.value) }
        }
    }
    TapHandler {
        enabled: slider.enabledControl
        // Node cards may overlap: consume track clicks rather than also
        // activating an inspector button on a card underneath this slider.
        gesturePolicy: TapHandler.WithinBounds
        property real beforeTap: 0
        onTapped: (point, button) => {
            // The second tap of a double-click opens the editor and undoes the first
            // tap's jump, so typing starts from the value the slider had.
            if (tapCount === 2 && slider.editable) {
                slider.commitValue(beforeTap)
                slider.openEditor()
                return
            }
            beforeTap = slider.value
            slider.forceActiveFocus()
            const x = Math.max(0, Math.min(track.width, point.position.x))
            slider.commitValue(slider.from + (x / track.width) * (slider.to - slider.from))
        }
    }
    }
    Rectangle {
        id: editor
        objectName: "vslider-editor"
        visible: false
        z: 10
        width: Math.min(76, slider.width)
        height: 24
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(0, Math.min(track.width - width, knob.x + knob.width / 2 - width / 2))
        radius: 6
        color: Theme.dialog
        border.width: 1
        border.color: Theme.accent
        TextInput {
            id: editorInput
            objectName: "vslider-editor-input"
            anchors.fill: parent
            anchors.leftMargin: 6; anchors.rightMargin: 6
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSmall
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            Keys.onReturnPressed: slider.closeEditor(true)
            Keys.onEnterPressed: slider.closeEditor(true)
            Keys.onEscapePressed: slider.closeEditor(false)
            onActiveFocusChanged: if (!activeFocus && editor.visible) slider.closeEditor(true)
        }
    }
    ToolButton {
        objectName: "vslider-reset"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 22; height: 20; padding: 0
        visible: slider.resettable
        enabled: slider.enabledControl && Math.abs(slider.value - slider.defaultValue) > 0.000001
        text: "↺"
        Accessible.name: qsTr("重置此参数")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("还原默认值：%1").arg(slider.defaultValue)
        contentItem: Text {
            text: parent.text; color: parent.enabled ? Theme.t2 : Theme.t3
            font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
        onClicked: {
            slider.commitValue(slider.defaultValue)
        }
    }
}
