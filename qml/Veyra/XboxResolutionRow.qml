import QtQuick

VRow {
    id: resolutionRow
    required property var xboxModel
    label: qsTr("串流分辨率")
    hint: qsTr("重新连接后生效。1440p 需要 Xbox Series X|S、支持的系统版本，以及 1440p 或 4K 主机显示设置。实际分辨率请查看串流统计。")
    VSelect {
        objectName: "xbox-stream-resolution"
        implicitWidth: 138
        value: resolutionRow.xboxModel?.state.resolution ? resolutionRow.xboxModel.state.resolution + "p" : qsTr("自动")
        options: [{ id: "0", label: qsTr("自动") }, { id: "1080", label: "1080p" }, { id: "1440", label: "1440p" }]
        onPicked: id => resolutionRow.xboxModel?.set("resolution", Number(id))
    }
}
