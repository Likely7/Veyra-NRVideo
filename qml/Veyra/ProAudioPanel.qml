// Professional-view audioCol controls; settings remain in the shared bridge.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: audioCol
    Layout.fillWidth: true
    spacing: 6
    VGroup {
        VRow {
            label: qsTr("音量")
            value: Math.round(veyra.volume * 100) + "%"
            VSlider {
                implicitWidth: 150
                from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                defaultValue: 1
                onMoved: value => veyra.volume = value
            }
        }
        VRow {
            label: qsTr("静音")
            VSwitch {
                checked: veyra.muted
                onToggled: checked => veyra.muted = checked
            }
        }
        VRow {
            visible: veyra.sourceKind === "capture"
            label: qsTr("本机音频监听")
            hint: qsTr("关闭后仍可录制和保存回放；本机不播放采集声音")
            VSwitch {
                objectName: "pro-capture-audio-monitor"
                checked: veyra.preferences.captureAudioMonitor !== false
                onToggled: v => veyra.setPreference("captureAudioMonitor", v)
            }
        }
        VRow {
            label: qsTr("声音同步")
            hint: veyra.audioSyncMode === 0 ? (veyra.audioSyncLive ? qsTr("软件估算补偿 ") + veyra.audioCompensationMs.toFixed(0) + " ms" : qsTr("自动估算 · 采集卡 / PS5 实时输入时生效"))
                  : veyra.audioSyncMode === 1 ? qsTr("手动偏移 · 实时输入时生效") : qsTr("关闭补偿")
            VSeg {
                options: [{ id: "0", label: qsTr("自动估算") }, { id: "1", label: qsTr("手动") }, { id: "2", label: qsTr("关闭", "off") }]
                current: String(veyra.audioSyncMode)
                onPicked: id => veyra.audioSyncMode = Number(id)
            }
        }
        VRow {
            label: qsTr("音频偏移")
            hint: qsTr("正值延后音频")
            value: veyra.audioOffsetMs + " ms"
            VSlider {
                implicitWidth: 150
                center: true
                from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                onMoved: value => veyra.audioOffsetMs = Math.round(value)
            }
        }
    }
    VEyebrow { Layout.leftMargin: Theme.s12; Layout.topMargin: Theme.s4; text: qsTr("音轨") }
    Text {
        Layout.leftMargin: Theme.s12
        visible: veyra.audioTracks.length === 0
        text: qsTr("当前源没有可选音轨。")
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
    }
    Repeater {
        model: veyra.audioTracks
        delegate: Rectangle {
            required property var modelData
            readonly property bool selected: veyra.selectedAudioTrack === modelData.index
            Layout.fillWidth: true
            implicitHeight: 44
            radius: Theme.rCard
            color: trackTap.pressed ? Theme.pressFill : trackHover.hovered ? Theme.card3 : Theme.card2
            border.width: 1
            border.color: activeFocus ? Theme.focusRing : selected ? Theme.accent : Theme.stroke
            activeFocusOnTab: true
            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.label
            Accessible.checked: selected
            Keys.onReturnPressed: veyra.selectedAudioTrack = modelData.index
            Keys.onSpacePressed: veyra.selectedAudioTrack = modelData.index
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s12
                spacing: Theme.s12
                VEqBars {
                    active: selected && veyra.running && !veyra.paused
                    opacity: selected ? 1 : 0.6
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: modelData.label
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    Text {
                        text: modelData.channels > 0 ? modelData.channels + qsTr(" 声道") : ""
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsEyebrow
                    }
                }
            }
            HoverHandler { id: trackHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: trackTap; onTapped: veyra.selectedAudioTrack = modelData.index }
        }
    }
}
