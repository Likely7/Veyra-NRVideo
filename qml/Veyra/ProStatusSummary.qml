// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    spacing: 5
    Text {
        objectName: "run-active-options"
        Layout.fillWidth: true
        visible: veyra.runOptions.length > 0
        text: qsTr("已开启：%1").arg(veyra.runOptions.join("；"))
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        HoverHandler { id: optionsHover }
        ToolTip.visible: optionsHover.hovered
        ToolTip.delay: 400
        ToolTip.text: text
    }
    ProStatusLight { Layout.fillWidth: true }
}
