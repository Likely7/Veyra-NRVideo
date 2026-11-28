// 设置 → 关于 → 性能诊断. veyra.perfDiagnostics records, analyses and packs;
// this dialog only starts it and shows what came out (PerfDiagnostics.h).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog
    objectName: "perf-diagnostics-dialog"
    parent: Overlay.overlay
    // A separate window: stays above the native video window, also in fullscreen.
    popupType: Popup.Window
    width: Math.min(520, parent ? parent.width - 32 : 520)
    height: body.implicitHeight + 36
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 18
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var diag: veyra.perfDiagnostics
    readonly property string state: diag ? diag.state : "idle"
    property int minutes: 5

    function clock(seconds) {
        const s = Math.max(0, Math.floor(seconds))
        return String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0")
    }
    function severityColor(severity) {
        return severity === "high" ? "#FF5D6C" : severity === "medium" ? Theme.warn : severity === "pass" ? "#3DDC84" : Theme.t2
    }

    background: Rectangle {
        objectName: "videoCover"
        property real coverRadius: 16
        radius: 16; color: Theme.dialog
        border.width: 1; border.color: Theme.stroke2
    }

    contentItem: ColumnLayout {
        id: body
        spacing: 14

        Text {
            text: qsTr("性能诊断")
            color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsH2; font.weight: Font.DemiBold
        }

        // --- idle: choose and start -----------------------------------------
        ColumnLayout {
            visible: dialog.state === "idle"
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
                text: qsTr("记录一段时间的帧时间、系统负载和日志，结束后自动分析并生成报告。记录期间照常使用，觉得卡的时候按 Ctrl+Alt+M 打个标记。数据只保存在本机，由你决定发给谁。")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text { text: qsTr("时长"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
                VSeg {
                    objectName: "perf-diagnostics-minutes"
                    options: [{ id: "3", label: qsTr("3 分钟") }, { id: "5", label: qsTr("5 分钟") }, { id: "10", label: qsTr("10 分钟") }]
                    current: String(dialog.minutes)
                    onPicked: id => dialog.minutes = Number(id)
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall
                text: qsTr("保存到：%1").arg(dialog.diag ? dialog.diag.outputRoot : "")
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.diag && !dialog.diag.analyzerAvailable
                wrapMode: Text.WordWrap
                color: Theme.warn; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall
                text: qsTr("未找到分析器（veyra_frame_analyzer.exe），结束后只保存原始数据。")
            }
        }

        // --- recording ---------------------------------------------------------
        ColumnLayout {
            visible: dialog.state === "recording"
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: qsTr("记录中 %1 / %2").arg(dialog.clock(dialog.diag ? dialog.diag.elapsedSeconds : 0))
                                          .arg(dialog.clock(dialog.diag ? dialog.diag.durationSeconds : 0))
                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 18
            }
            Rectangle {
                Layout.fillWidth: true
                height: 4; radius: 2; color: Theme.stroke2
                Rectangle {
                    height: parent.height; radius: 2; color: Theme.accent
                    width: parent.width * (dialog.diag && dialog.diag.durationSeconds > 0
                                           ? Math.min(1, dialog.diag.elapsedSeconds / dialog.diag.durationSeconds) : 0)
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
                text: qsTr("可以关闭这个窗口继续使用。觉得卡时按 Ctrl+Alt+M 标记（已标记 %1 次）。").arg(dialog.diag ? dialog.diag.marks : 0)
            }
        }

        // --- analysing -----------------------------------------------------
        RowLayout {
            visible: dialog.state === "analyzing"
            spacing: 10
            BusyIndicator { running: dialog.state === "analyzing"; implicitWidth: 28; implicitHeight: 28 }
            Text { text: qsTr("正在分析并打包，通常需要几秒到半分钟…"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
        }

        // --- result ----------------------------------------------------------
        ColumnLayout {
            visible: dialog.state === "done" || dialog.state === "failed"
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                visible: dialog.diag && dialog.diag.headline.length > 0
                wrapMode: Text.WordWrap
                color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
                text: dialog.diag ? dialog.diag.headline : ""
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.diag && dialog.diag.message.length > 0
                wrapMode: Text.WordWrap
                color: dialog.state === "failed" ? Theme.warn : Theme.t2
                font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
                text: dialog.diag ? dialog.diag.message : ""
            }
            Repeater {
                model: dialog.diag ? dialog.diag.findings : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: findingColumn.implicitHeight + 20
                    radius: 10
                    color: Theme.card2
                    Rectangle { width: 3; radius: 1.5; anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 8 }
                                color: dialog.severityColor(modelData.severity) }
                    ColumnLayout {
                        id: findingColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 20; rightMargin: 12; topMargin: 10 }
                        spacing: 4
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.title; color: Theme.t1
                               font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.advice; color: Theme.t2
                               font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: modelData.evidence.length > 0
                               text: modelData.evidence; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.diag && dialog.diag.zipPath.length > 0
                wrapMode: Text.WrapAnywhere
                color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall
                text: qsTr("诊断包：%1（反馈时把它发给开发者）").arg(dialog.diag ? dialog.diag.zipPath : "")
            }
        }

        // --- buttons ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            VButton {
                visible: dialog.state === "idle"
                text: qsTr("取消"); onClicked: dialog.close()
            }
            VButton {
                objectName: "perf-diagnostics-start"
                visible: dialog.state === "idle"
                primary: true
                text: qsTr("开始诊断")
                onClicked: if (dialog.diag && dialog.diag.start(dialog.minutes)) dialog.close()
            }
            VButton {
                visible: dialog.state === "recording"
                text: qsTr("标记")
                onClicked: dialog.diag.mark()
            }
            VButton {
                objectName: "perf-diagnostics-stop"
                visible: dialog.state === "recording"
                text: qsTr("提前结束")
                onClicked: dialog.diag.stop()
            }
            VButton {
                visible: dialog.state === "recording" || dialog.state === "analyzing"
                text: qsTr("隐藏"); onClicked: dialog.close()
            }
            VButton {
                visible: dialog.state === "done"
                primary: true
                text: qsTr("打开报告"); onClicked: dialog.diag.openReport()
            }
            VButton {
                visible: dialog.state === "done" || dialog.state === "failed"
                text: qsTr("打开所在文件夹"); onClicked: dialog.diag.openFolder()
            }
            VButton {
                visible: dialog.state === "done" && dialog.diag && dialog.diag.zipPath.length > 0
                text: qsTr("复制路径"); onClicked: dialog.diag.copyPath()
            }
            VButton {
                visible: dialog.state === "done" || dialog.state === "failed"
                text: qsTr("完成")
                onClicked: { dialog.diag.dismiss(); dialog.close() }
            }
        }
    }
}
