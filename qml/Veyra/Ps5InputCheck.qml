import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "Ps5Bindings.js" as Bindings

FocusScope {
    id: check
    objectName: "ps5-input-check"
    required property var settings
    required property bool offer
    required property bool startAfterCheck
    property bool localControl: false
    property bool localOverlay: false
    property string lastKey: ""
    property bool moved: false
    signal tryRequested()
    signal finishRequested(bool start)

    function monitor() {
        if (!settings) return
        if (!offer && visible && Window.window && Window.window.active) {
            settings.inputDevices.start(Window.window)
            pad.forceActiveFocus()
        } else {
            settings.inputDevices.stop()
            localControl = false
            localOverlay = false
        }
    }
    onOfferChanged: Qt.callLater(monitor)
    Component.onCompleted: Qt.callLater(monitor)
    Component.onDestruction: if (settings) settings.inputDevices.stop()
    Connections {
        target: check.Window.window
        function onActiveChanged() { check.monitor() }
    }
    Flickable {
        id: viewport
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, content.implicitHeight + 40)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: content
            width: Math.min(600, viewport.width - 48)
            x: (viewport.width - width) / 2
            y: (viewport.contentHeight - implicitHeight) / 2
            spacing: 10
            Label { text: qsTr("试用控制"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 25; font.bold: true }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.t2
                font.family: Theme.fontUi
                text: check.offer ? qsTr("开始游戏前，可以检查设备并练习切换控制。也可稍后在“设备与快捷键”中进行。")
                                  : qsTr("在预览中移动鼠标并按 W。输入仅在本机生效。")
            }
            Rectangle {
                id: pad
                objectName: "practice-pad"
                visible: !check.offer
                activeFocusOnTab: true
                Accessible.role: Accessible.Pane
                Accessible.name: qsTr("本机输入预览")
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: status.implicitHeight + 32
                radius: 8; color: Theme.card2
                border.color: activeFocus ? Theme.accent : Theme.stroke2
                HoverHandler { onPointChanged: if (hovered) check.moved = true }
                TapHandler { onTapped: pad.forceActiveFocus() }
                Keys.onPressed: event => {
                    const code = check.settings.keyCode(event.key, event.nativeScanCode)
                    const plain = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier
                    const control = plain && code === check.settings.controlKey
                    const overlay = plain && code === check.settings.overlayKey
                    if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) && !control && !overlay) return
                    event.accepted = true
                    if (event.isAutoRepeat) return
                    check.lastKey = Bindings.caption(code) || event.text
                    const emergency = Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier
                    if (event.key === Qt.Key_Z && (event.modifiers & emergency) === emergency) {
                        check.localControl = false
                        check.localOverlay = false
                    } else if (control) {
                        check.localControl = !check.localControl
                        if (!check.localControl) check.localOverlay = false
                    } else if (overlay && check.localControl) {
                        check.localOverlay = !check.localOverlay
                    }
                }
                ColumnLayout {
                    id: status
                    anchors.verticalCenter: parent.verticalCenter
                    x: 16; width: parent.width - 32; spacing: 8
                    Label { Layout.fillWidth: true; text: check.localControl ? qsTr("预览 · PS5 控制") : qsTr("预览 · Windows 控制"); color: Theme.accent; font.bold: true }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke2 }
                    Label { Layout.fillWidth: true; elide: Text.ElideMiddle; color: Theme.t1; text: qsTr("键盘：%1").arg([check.settings.inputDevices.keyboard, check.lastKey].filter(Boolean).join(" · ") || "—") }
                    Label { Layout.fillWidth: true; elide: Text.ElideMiddle; color: Theme.t1; text: qsTr("鼠标：%1").arg([check.settings.inputDevices.mouse, check.moved ? qsTr("已检测到移动") : ""].filter(Boolean).join(" · ") || "—") }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.t2; text: qsTr("切换控制 · %1     显示按键 · %2").arg(Bindings.shortcutName(check.settings.controlKey)).arg(Bindings.shortcutName(check.settings.overlayKey)) }
                    Label { Layout.fillWidth: true; visible: check.localOverlay; text: qsTr("按键浮层") + " · " + Bindings.situationName(check.settings.profile, check.settings.situation || (check.settings.profile.contexts || [])[0]); color: Theme.t1 }
                    Repeater {
                        model: check.localOverlay ? Bindings.records(check.settings.profile,
                            check.settings.situation || (check.settings.profile.contexts || [])[0]).slice(0, 4) : []
                        RowLayout {
                            id: example
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Label {
                                Layout.preferredWidth: Math.min(146, status.width * 0.35)
                                text: example.modelData.input
                                wrapMode: Text.Wrap; color: Theme.accent
                            }
                            Label {
                                Layout.fillWidth: true
                                text: example.modelData.shortTitle
                                wrapMode: Text.Wrap; color: Theme.t1
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.topMargin: 6
                spacing: 8
                Ps5EditorButton { objectName: "practice-try"; visible: check.offer; text: qsTr("试用控制"); primary: true; onClicked: check.tryRequested() }
                Ps5EditorButton { objectName: "practice-skip"; visible: check.offer; text: qsTr("跳过"); onClicked: check.finishRequested(true) }
                Ps5EditorButton { objectName: "practice-done"; visible: !check.offer; text: qsTr("完成"); onClicked: check.finishRequested(false) }
                Ps5EditorButton { visible: !check.offer && check.startAfterCheck; text: qsTr("控制 PS5"); primary: true; enabled: check.settings.available; onClicked: check.finishRequested(true) }
            }
        }
    }
}
