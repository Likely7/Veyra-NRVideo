// The timing bar: a thin strip under the video showing where each stage spent
// its time, with the labels drawn ON the bar rather than in a legend below it.
//
// Every number comes from the engine. A stage with no measurement is drawn as
// unmeasured, never as zero.
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    implicitHeight: 22

    // [{ label, ms, color, measured }]
    property var stages: []
    readonly property real measuredTotal: {
        let total = 0
        for (let i = 0; i < stages.length; ++i)
            if (stages[i].measured) total += stages[i].ms
        return total
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.rCtl
        color: Theme.card
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: root.stages
                delegate: Item {
                    required property var modelData
                    readonly property string fullLabel: modelData.measured
                        ? modelData.label + " " + modelData.ms.toFixed(1)
                        : modelData.label + " —"
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 28
                    Layout.preferredWidth: {
                        if (!modelData.measured) return 52
                        if (root.measuredTotal <= 0) return 44
                        return Math.max(40, modelData.ms / root.measuredTotal * root.width)
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: Theme.rCtl - 2
                        color: modelData.measured ? modelData.color : Theme.card3
                        opacity: modelData.measured ? 0.92 : 0.5
                    }
                    Text {
                        anchors.centerIn: parent
                        text: parent.fullLabel
                        color: modelData.measured ? Theme.accentInk : Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsEyebrow
                        elide: Text.ElideRight
                        width: Math.max(0, parent.width - Theme.s8)
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
