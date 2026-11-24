import QtQuick

VRow {
    readonly property var download: veyra.anrDownload || ({})
    visible: !!download.active
    label: "ANR FG"
    hint: download.status || ""
    value: visible ? download.percent + "%" : ""
    VButton {
        text: qsTr("取消")
        onClicked: veyra.cancelAnrDownload()
    }
}
