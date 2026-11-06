// One owner, one parameter model. Switching views never copies or resets settings.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var layerData
    property int layerCount: 1
    property bool nrFirst: false
    property bool listControls: true
    readonly property bool webEnabled: veyra.preferences.nrWebEditor === true
    readonly property bool amdNr: layerData.runtime === 4
    readonly property string amdRuntimeLabel: sliderEditor ? sliderEditor.amdRuntimeLabel : ""
    readonly property string sizeLabel: sliderEditor ? sliderEditor.sizeLabel : ""
    readonly property bool manualSize: layerData.manualSize ?? false
    signal edited(int nodeIndex, string key, double amount)
    signal duplicateRequested(int nodeIndex)
    signal removeRequested(int nodeIndex)
    signal orderEdited(bool enabled)
    spacing: 8

    VRow {
        label: qsTr("网状调节器")
        hint: qsTr("开启后在下方统一调节；原滑条保留并置灰，数值同步。")
        VSwitch {
            objectName: "nr-web-toggle"
            checked: editor.webEnabled
            onToggled: checked => veyra.setPreference("nrWebEditor", checked)
        }
    }
    Loader {
        id: webLoader
        objectName: "nr-web-editor"
        Layout.fillWidth: true
        active: editor.webEnabled
        sourceComponent: Component {
            NrParameterEditor {
                layerData: editor.layerData
                layerCount: editor.layerCount
                nrFirst: editor.nrFirst
                listControls: editor.listControls
                webMode: true
                lastManualPercent: sliderEditor ? sliderEditor.lastManualPercent : 100
                onEdited: (i, k, v) => { if (editor.webEnabled) editor.edited(i, k, v) }
                onDuplicateRequested: i => { if (editor.webEnabled) editor.duplicateRequested(i) }
                onRemoveRequested: i => { if (editor.webEnabled) editor.removeRequested(i) }
                onOrderEdited: value => { if (editor.webEnabled) editor.orderEdited(value) }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: editor.webEnabled
        text: qsTr("原滑条调节 · 网状调节器已接管")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12; wrapMode: Text.Wrap
    }
    NrParameterEditor {
        id: sliderEditor
        objectName: "nr-slider-editor"
        Layout.fillWidth: true
        layerData: editor.layerData
        layerCount: editor.layerCount
        nrFirst: editor.nrFirst
        listControls: editor.listControls
        enabled: !editor.webEnabled
        opacity: enabled ? 1 : 0.4
        onEdited: (i, k, v) => { if (!editor.webEnabled) editor.edited(i, k, v) }
        onDuplicateRequested: i => { if (!editor.webEnabled) editor.duplicateRequested(i) }
        onRemoveRequested: i => { if (!editor.webEnabled) editor.removeRequested(i) }
        onOrderEdited: value => { if (!editor.webEnabled) editor.orderEdited(value) }
    }
}
