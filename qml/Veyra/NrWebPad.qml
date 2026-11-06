// Direct parameter editing, not a quality score or a hue/saturation warper.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: pad
    property var axes: [] // key, label, hint, min, max, defaultValue
    property var values: ({})
    property string identity: ""
    property bool editable: true
    property int selectedAxis: 0
    readonly property var selected: axes.length ? axes[Math.max(0, Math.min(axes.length - 1, selectedAxis))] : ({})
    property bool dragging: false
    property real startValue: 0
    property real draft: 0
    property string gestureIdentity: ""
    property var undoEdit: null
    property bool ready: false
    property string numericIdentity: ""
    property string numericKey: ""
    signal edited(string key, real amount)
    spacing: 6

    function valueAt(i) {
        const a = axes[i]
        return Math.max(a.min, Math.min(a.max, Number(values[a.key] ?? a.defaultValue)))
    }
    function fractionAt(i, defaults) {
        const a = axes[i]
        const v = defaults ? a.defaultValue : (dragging && i === selectedAxis ? draft : valueAt(i))
        return (v - a.min) / (a.max - a.min)
    }
    function angleAt(i) { return -Math.PI / 2 + i * Math.PI * 2 / axes.length }
    function pointAt(i, fraction) {
        return Qt.point(surface.width / 2 + Math.cos(angleAt(i)) * surface.radius * fraction,
                        surface.height / 2 + Math.sin(angleAt(i)) * surface.radius * fraction)
    }
    function chooseAxis(x, y) {
        const dx = x - surface.width / 2, dy = y - surface.height / 2
        if (Math.hypot(dx, dy) < 12) return selectedAxis
        const turns = (Math.atan2(dy, dx) + Math.PI / 2 + Math.PI * 2) % (Math.PI * 2)
        return Math.round(turns * axes.length / (Math.PI * 2)) % axes.length
    }
    function updateDraft(x, y) {
        const a = axes[selectedAxis], angle = angleAt(selectedAxis)
        const distance = (x - surface.width / 2) * Math.cos(angle) + (y - surface.height / 2) * Math.sin(angle)
        draft = Math.round((a.min + Math.max(0, Math.min(1, distance / surface.radius)) * (a.max - a.min)) * 1000) / 1000
        surface.requestPaint()
        if (!live.running) live.start()
    }
    function begin(x, y) {
        if (!editable || !enabled || !axes.length) return false
        const chosen = chooseAxis(x, y), angle = angleAt(chosen)
        const across = Math.abs((x - surface.width / 2) * Math.sin(angle) - (y - surface.height / 2) * Math.cos(angle))
        if (across > 20 || Math.hypot(x - surface.width / 2, y - surface.height / 2) > surface.radius + 18) return false
        surface.forceActiveFocus() // finish an old numeric field before changing its axis
        selectedAxis = chosen
        startValue = valueAt(selectedAxis); gestureIdentity = identity
        dragging = true; surface.forceActiveFocus(); updateDraft(x, y)
        return true
    }
    function finish(apply) {
        if (!dragging) return
        live.stop()
        const valid = gestureIdentity === identity && editable && enabled
        const a = axes[selectedAxis], value = apply ? draft : startValue
        dragging = false
        if (valid) {
            if (apply && value !== startValue) undoEdit = {key:a.key, before:startValue, identity:identity}
            edited(a.key, value)
        }
        surface.requestPaint()
    }
    function abandon() { live.stop(); dragging = false; surface.requestPaint() }
    function selectAxis(i) { finish(true); surface.forceActiveFocus(); selectedAxis = i; surface.requestPaint() }
    function captureNumericOwner() { numericIdentity = identity; numericKey = axes.length ? axes[selectedAxis].key : "" }
    function setSelected(value) {
        if (!enabled || !editable || !axes.length) return
        const a = axes[selectedAxis], before = valueAt(selectedAxis)
        const next = Math.max(a.min, Math.min(a.max, value))
        if (before !== next) undoEdit = {key:a.key, before:before, identity:identity}
        edited(a.key, next)
    }
    onIdentityChanged: if (ready) {
        abandon(); undoEdit = null; numericValue.closeEditor(false); valueSlider.closeEditor(false)
        if (!valueSlider.pressed) captureNumericOwner()
    }
    onEditableChanged: if (ready && !editable) abandon()
    onEnabledChanged: if (ready && !enabled) abandon()
    onVisibleChanged: if (ready) { if (!visible) abandon(); else surface.requestPaint() }
    onValuesChanged: if (ready && visible) surface.requestPaint()
    onAxesChanged: if (ready) { selectedAxis = Math.max(0, Math.min(selectedAxis, axes.length - 1)); surface.requestPaint() }
    onSelectedAxisChanged: if (ready) { surface.requestPaint(); if (!valueSlider.pressed) captureNumericOwner() }
    Component.onCompleted: { ready = true; captureNumericOwner() }
    Component.onDestruction: live.stop()
    Timer {
        id: live
        interval: 40
        onTriggered: if (pad.dragging && pad.gestureIdentity === pad.identity && pad.editable && pad.enabled)
                         pad.edited(pad.axes[pad.selectedAxis].key, pad.draft)
    }

    Canvas {
        id: surface
        objectName: pad.objectName + "-surface"
        Layout.fillWidth: true
        Layout.preferredHeight: pad.axes.length > 6 ? Math.max(330, Math.min(400, width * 1.18)) : Math.min(340, Math.max(270, width))
        readonly property real radius: Math.max(36, Math.min(width / 2 - 67, height / 2 - 53))
        activeFocusOnTab: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onActiveFocusChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset()
            if (!pad.axes.length) return
            function polygon(fraction, defaults, current) {
                c.beginPath()
                for (let i = 0; i <= pad.axes.length; ++i) {
                    const k = i % pad.axes.length
                    const p = pad.pointAt(k, current ? pad.fractionAt(k, defaults) : fraction)
                    if (i === 0) c.moveTo(p.x, p.y); else c.lineTo(p.x, p.y)
                }
                c.closePath()
            }
            c.lineWidth = 1; c.strokeStyle = Theme.stroke2
            for (let ring = 1; ring <= 4; ++ring) { polygon(ring / 4, false, false); c.stroke() }
            for (let i = 0; i < pad.axes.length; ++i) {
                const p = pad.pointAt(i, 1)
                c.beginPath(); c.moveTo(width / 2, height / 2); c.lineTo(p.x, p.y); c.stroke()
            }
            c.strokeStyle = Theme.t3; c.setLineDash([3, 4]); polygon(0, true, true); c.stroke(); c.setLineDash([])
            polygon(0, false, true); c.fillStyle = Theme.accentSoft; c.fill(); c.strokeStyle = Theme.accent; c.lineWidth = 2; c.stroke()
            for (let i = 0; i < pad.axes.length; ++i) {
                const p = pad.pointAt(i, pad.fractionAt(i, false))
                c.beginPath(); c.arc(p.x, p.y, i === pad.selectedAxis ? 6 : 4, 0, Math.PI * 2)
                c.fillStyle = i === pad.selectedAxis ? Theme.t1 : Theme.accent; c.fill()
            }
            if (activeFocus) {
                const p = pad.pointAt(pad.selectedAxis, 1)
                c.strokeStyle = Theme.accent; c.lineWidth = 2
                c.beginPath(); c.moveTo(width / 2, height / 2); c.lineTo(p.x, p.y); c.stroke()
            }
        }
        Keys.onShortcutOverride: event => {
            if (pad.editable && pad.enabled && [Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down,Qt.Key_Escape,Qt.Key_Return,Qt.Key_Enter].indexOf(event.key) >= 0) event.accepted = true
        }
        Keys.onPressed: event => {
            if (!pad.editable || !pad.enabled) return
            if (event.key === Qt.Key_Escape) { pad.finish(false); event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { valueSlider.openEditor(); event.accepted = true }
            else if ([Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down].indexOf(event.key) >= 0) {
                const direction = event.key === Qt.Key_Left || event.key === Qt.Key_Down ? -1 : 1
                const step = Number(veyra.preferences.sliderKeyStep ?? 0.01)
                pad.setSelected(Math.round((pad.valueAt(pad.selectedAxis) + direction * step) * 1000) / 1000)
                event.accepted = true
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: pad.editable && pad.enabled
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: mouse => { mouse.accepted = pad.begin(mouse.x, mouse.y) }
            onPositionChanged: mouse => { if (pressed && pad.dragging) pad.updateDraft(mouse.x, mouse.y) }
            onReleased: pad.finish(true)
            onCanceled: pad.finish(false)
            onDoubleClicked: { pad.finish(false); pad.setSelected(pad.selected.defaultValue) }
        }
        Repeater {
            model: pad.axes
            delegate: Item {
                required property var modelData
                required property int index
                objectName: pad.objectName + "-axis-" + modelData.key
                readonly property point end: pad.pointAt(index, 1)
                readonly property real dx: Math.cos(pad.angleAt(index))
                readonly property real dy: Math.sin(pad.angleAt(index))
                width: pad.axes.length > 6 ? Math.min(92, Math.max(48, surface.width * 0.25)) : Math.min(106, Math.max(76, surface.width * 0.27))
                height: pad.axes.length > 6 ? 54 : 50
                x: Math.max(0, Math.min(surface.width - width, end.x + dx * (pad.axes.length > 6 ? 46 : 34) - width / 2))
                y: pad.axes.length > 6 ? surface.height / 2 + dy * (surface.height / 2 - height / 2 - 6) - height / 2 : end.y + dy * 23 - height / 2
                Text {
                    anchors.fill: parent
                    text: modelData.label + "\n" + (pad.dragging && index === pad.selectedAxis ? pad.draft : pad.valueAt(index)).toFixed(2)
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    color: index === pad.selectedAxis ? Theme.t1 : Theme.t2
                    font.family: Theme.fontUi; font.pixelSize: 11
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: pad.selectAxis(index) }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("实线：当前值 · 虚线：默认值；向外增加该项，并非越大越好。")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: pad.axes.length > 0
        spacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: pad.selected.label ?? ""; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12; wrapMode: Text.Wrap }
            Text { Layout.fillWidth: true; text: pad.selected.hint ?? ""; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.Wrap }
        }
        VSlider {
            id: valueSlider
            objectName: pad.objectName + "-value"
            Layout.preferredWidth: 110
            valueFromModel: true; resettable: true
            enabledControl: pad.editable
            from: pad.selected.min ?? 0; to: pad.selected.max ?? 1
            defaultValue: pad.selected.defaultValue ?? 0
            value: pad.axes.length ? pad.valueAt(pad.selectedAxis) : 0
            onPressedChanged: if (pressed) pad.captureNumericOwner()
            onEditingChanged: if (editing) pad.captureNumericOwner()
            onActiveFocusChanged: if (activeFocus) pad.captureNumericOwner()
            onMoved: value => {
                if (pad.numericIdentity !== pad.identity || pad.numericKey !== pad.axes[pad.selectedAxis].key) {
                    if (!valueSlider.pressed) pad.captureNumericOwner()
                    return // every stale live update and the release are ignored
                }
                pad.setSelected(value)
            }
        }
        VSliderValue {
            id: numericValue
            objectName: pad.objectName + "-number"
            Layout.minimumWidth: 40
            slider: valueSlider
            text: (pad.axes.length ? pad.valueAt(pad.selectedAxis) : 0).toFixed(2)
            onEditingChanged: if (editing) pad.captureNumericOwner()
        }
    }
    VButton {
        objectName: pad.objectName + "-undo"
        text: qsTr("撤销上一项调整"); ghost: true
        enabled: pad.editable && pad.undoEdit !== null && pad.undoEdit.identity === pad.identity
        onClicked: { const last = pad.undoEdit; pad.undoEdit = null; pad.edited(last.key, last.before) }
    }
    Connections { target: Theme; function onAccentNameChanged() { surface.requestPaint() } }
}
