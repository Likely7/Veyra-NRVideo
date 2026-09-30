// .spin (pages.css): a 14px ring, 2px, the top quarter bright, turning once
// every .8s linear (M38). Runs only while visible; reduced motion keeps the
// ring still, since a busy state is already said by the text beside it.
import QtQuick

Item {
    id: spin
    property int size: 14
    property color color: "#FFFFFF"
    implicitWidth: size
    implicitHeight: size
    Canvas {
        id: ring
        anchors.fill: parent
        onPaint: {
            const g = getContext("2d"); g.reset()
            const r = width / 2 - 1.5
            g.lineWidth = 2
            g.strokeStyle = Qt.rgba(1, 1, 1, 0.2)
            g.beginPath(); g.arc(width / 2, height / 2, r, 0, Math.PI * 2); g.stroke()
            g.strokeStyle = spin.color
            g.beginPath(); g.arc(width / 2, height / 2, r, -Math.PI / 2, 0); g.stroke()
        }
        RotationAnimation on rotation {
            from: 0; to: 360
            duration: 800
            loops: Animation.Infinite
            running: spin.visible && !Theme.reduced
        }
    }
}
