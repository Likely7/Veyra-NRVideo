import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property string namePrefix
    required property var params
    signal edited(string channel, real amount)
    spacing: 4

    Text {
        Layout.fillWidth: true
        text: qsTr("单帧保边降噪，可分别调节亮度颗粒和彩色噪点。")
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    component StrengthRow: VRow {
        id: row
        required property string channel
        required property real fallback
        readonly property real strength: editor.params[channel] ?? fallback
        value: Math.round(strength * 100) + "%"
        VSlider {
            objectName: editor.namePrefix + "-" + row.channel
            implicitWidth: 110
            from: 0
            to: 1
            stepSize: 0.01
            inputScale: 100
            value: row.strength
            valueFromModel: true
            resettable: true
            defaultValue: row.fallback
            onMoved: amount => editor.edited(row.channel, Math.round(amount * 100) / 100)
        }
    }

    StrengthRow { channel: "luma"; label: qsTr("亮度降噪"); fallback: 0.20 }
    StrengthRow { channel: "chroma"; label: qsTr("彩噪抑制"); fallback: 0.60 }
}
