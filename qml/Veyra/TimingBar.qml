// The timing bar: a thin strip under the video showing where each stage spent
// its time, with the labels drawn ON the bar rather than in a legend below it.
// The user asked for this explicitly (the first version was too tall and
// carried a separate legend).
//
// Every number comes from the engine. A stage with no measurement is drawn as
// unmeasured, never as zero: a bar that shows 0 ms for something we did not
// measure is a lie about the latency this product is sold on.
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    implicitHeight: 22

    // [{ label, ms, color, measured }]
    property var stages: []

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Theme.card
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: root.stages
                delegate: Item {
                    required property var modelData
                    // A stage's width is its measured share; unmeasured stages
                    // get an equal sliver so they stay legible.
                    readonly property real measuredTotal: root.stages.reduce(
                        (a, s) => a + (s.measured ? s.ms : 0), 0)
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: {
                        if (!modelData.measured) return 58
                        if (measuredTotal <= 0) return 0
                        return Math.max(46, modelData.ms / measuredTotal * root.width)
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 3
                        color: modelData.measured ? modelData.color : Theme.card3
                        opacity: modelData.measured ? 0.92 : 0.5
                    }
                    Text {
                        anchors.centerIn: parent
                        // Label and number on the bar itself.
                        text: modelData.measured
                              ? modelData.label + " " + modelData.ms.toFixed(1)
                              : modelData.label + " —"
                        color: modelData.measured ? Theme.accentInk : Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
