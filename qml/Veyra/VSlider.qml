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
        Behavior on scale { NumberAnimation { duration: Theme.d(350); easing.bezierCurve: Theme.spring } }
        // box-shadow: 0 2px 6px rgba(0,0,0,.5)
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.5)
            shadowVerticalOffset: 2
            shadowBlur: 0.5
            blurMax: 12
            autoPaddingEnabled: true
        }
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
        onTapped: point => {
            const x = Math.max(0, Math.min(track.width, point.position.x))
            slider.commitValue(slider.from + (x / track.width) * (slider.to - slider.from))
        }
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
