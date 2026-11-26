import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic

DialogLayer {
    id: dialog
    required property var update
    property bool exportRunning: false
    readonly property bool working: update.downloading || update.installing
    readonly property string detail: update.installing
        ? [qsTr("校验更新包"), qsTr("解压更新包"), qsTr("检查安装文件"), qsTr("验证新版启动")][update.installStep]
        : update.downloading ? qsTr("正在下载更新 %1%").arg(update.progress) : ""

    key: "update"
    objectName: "software-update-dialog"
    dialogWidth: 480
    minimumBodyWidth: 0
    glyph: "download"
    title: update.restarting ? qsTr("正在重启 Veyra")
         : update.ready ? qsTr("重启完成更新")
         : update.installing ? qsTr("正在安装更新")
         : update.downloading ? qsTr("正在下载更新")
         : qsTr("软件更新")
    sub: update.targetVersion.length > 0 ? "Veyra " + update.targetVersion : ""
    actions: {
        if (update.installing || update.restarting) return []
        if (update.downloading) return [{label: qsTr("取消下载")}]
        if (update.ready) return [
            {label: qsTr("稍后")},
            {label: qsTr("立即重启"), primary: true, enabled: !exportRunning}
        ]
        const buttons = [{label: qsTr("关闭")}]
        if (update.available) buttons.push({label: qsTr("立即更新"), primary: true})
        return buttons
    }
    onActionTriggered: label => {
        if (label === qsTr("取消下载")) {
            update.cancel()
            dialogHost.close()
        } else if (label === qsTr("立即重启")) {
            update.restart()
        } else if (label === qsTr("立即更新")) {
            update.start()
        } else {
            dialogHost.close()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            visible: dialog.working
            Text {
                Layout.fillWidth: true
                text: dialog.detail
                wrapMode: Text.WordWrap
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
            Text {
                text: dialog.update.installing
                      ? qsTr("步骤 %1/%2").arg(dialog.update.installStep + 1).arg(4)
                      : dialog.update.sizeText
                color: Theme.t3
                font.family: Theme.fontMono
                font.pixelSize: 11
            }
        }

        Basic.ProgressBar {
            id: progress
            objectName: "software-update-progress"
            Layout.fillWidth: true
            visible: dialog.working
            from: 0
            to: dialog.update.installing ? 4 : 100
            value: dialog.update.installing ? dialog.update.installStep : dialog.update.progress
            Accessible.name: dialog.detail
            background: Rectangle {
                implicitHeight: 6
                radius: 3
                color: Theme.stroke2
            }
            contentItem: Item {
                Rectangle {
                    width: progress.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.update.errorText.length > 0 ? dialog.update.errorText
                : dialog.update.restarting ? ""
                : dialog.update.ready && dialog.exportRunning ? qsTr("导出完成后可重启")
                : dialog.update.ready ? qsTr("新版已安装，重启 Veyra 后生效。")
                : dialog.update.available ? dialog.update.releaseNotes : ""
            color: Theme.t2
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
    }
}
