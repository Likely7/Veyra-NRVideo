// .trk .bars (pages.css, M39): four thin bars beside an audio track. Idle they
// sit at 30 %; on the playing track they bounce 25 % <-> 100 % over 1s,
// ease-in-out, each bar out of phase (-.3s, -.6s, -.15s).
import QtQuick

Item {
    id: eq
    property bool active: false
    property color color: Theme.accent
    implicitWidth: 4 * 3 + 3 * 2
    implicitHeight: 14
    // One linear 0..1 clock; each bar reads it with its own phase offset, the
    // cosine giving the design's ease-in-out between 25 % and 100 %.
    property real phase: 0
    NumberAnimation on phase {
        from: 0; to: 1
        duration: 1000
        loops: Animation.Infinite
        running: eq.active && eq.visible && !Theme.reduced
    }
    Row {
        anchors.fill: parent
        spacing: 2
        Repeater {
            model: [0, 0.3, 0.6, 0.15]
            delegate: Item {
                required property real modelData
                width: 3
                height: eq.height
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    radius: 1
                    color: eq.active ? eq.color : Theme.t3
                    readonly property real level: !eq.active ? 0.3
                        : Theme.reduced ? 0.6
                        : 0.625 - 0.375 * Math.cos(2 * Math.PI * (eq.phase + modelData))
                    height: Math.max(2, parent.height * level)
                }
            }
        }
    }
}
