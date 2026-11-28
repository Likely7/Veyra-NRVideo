// Keyboard and mouse as the PS5 pad: which key or mouse button does which pad action.
// The defaults follow the GTA VI default control scheme; every row can take several inputs
// and an input does one action (binding it again moves it).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: keysDialog
    objectName: "ps5-keys-dialog"
    key: "ps5keys"
    glyph: "gamepad"
    title: qsTr("键鼠按键设置")
    sub: qsTr("默认按 GTA 6 默认操作方案。按键按键盘上的位置保存：AZERTY 键盘上 WASD 就是 ZQSD")
    dialogWidth: 640
    actions: [
        { label: qsTr("恢复 GTA 6 默认") },
        { label: qsTr("完成"), primary: true }
    ]
    onActionTriggered: label => {
        if (label === qsTr("恢复 GTA 6 默认")) { listening = ""; veyra.ps5Set("resetKeys", true); return }
        listening = ""
        dialogHost.close()
    }
    onShownChanged: if (!shown) listening = ""

    // The action waiting for its new input, "" when none.
    property string listening: ""
    readonly property var labels: ({
        stickUp: qsTr("左摇杆 上 · 前进"),
        stickDown: qsTr("左摇杆 下 · 后退"),
        stickLeft: qsTr("左摇杆 左"),
        stickRight: qsTr("左摇杆 右"),
        cross: qsTr("✕ · 跳跃、攀爬、手刹"),
        circle: qsTr("○ · 蹲下、车内躲避"),
        square: qsTr("□ · 近战、推车"),
        triangle: qsTr("△ · 互动、上下车"),
        l1: qsTr("L1 · 收枪（点按）、装备轮盘（按住）"),
        r1: qsTr("R1 · 掩体、快速投掷、车上射击"),
        l2: qsTr("L2 · 瞄准、刹车"),
        r2: qsTr("R2 · 射击、油门"),
        l3: qsTr("L3 · 奔跑、喇叭和氮气"),
        r3: qsTr("R3 · 回头看、瞄准时缩放"),
        focus: qsTr("L3+R3 · 专注（Focus）"),
        up: qsTr("方向键上 · 手机"),
        down: qsTr("方向键下 · 目标和小地图"),
        left: qsTr("方向键左 · 电台、载具菜单"),
        right: qsTr("方向键右 · 同伴菜单"),
        options: qsTr("Options · 暂停"),
        share: qsTr("Create"),
        touchpad: qsTr("触摸板 · 地图"),
        ps: qsTr("PS 键"),
        vehicle: qsTr("切换步行/载具（载具时前进键油门、后退键刹车、射击键车上射击）")
    })
    function bind(code) {
        if (listening.length === 0 || code <= 0) return
        veyra.ps5Set("bindKey", { code: code, action: listening })
        listening = ""
    }

    // Waiting for an input: keys come through the focused box, mouse buttons and the wheel
    // through a press inside it. The scan code is the key's position (with 0x100 for an
    // extended key), the same code the capture feeds the pad.
    DialogGroup {
        visible: keysDialog.listening.length > 0
        VRow {
            label: qsTr("按下新按键")
            hint: keysDialog.labels[keysDialog.listening] || ""
            RowLayout {
                spacing: 6
                Rectangle {
                    id: listenBox
                    objectName: "ps5-keys-listen"
                    implicitWidth: 220; implicitHeight: 44; radius: 10
                    color: Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: activeFocus ? Theme.stroke2 : "transparent"
                    focus: keysDialog.listening.length > 0
                    onVisibleChanged: if (visible) forceActiveFocus()
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("按键盘键，或在这里按鼠标键/滚轮")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSmall
                    }
                    // Esc, Tab and the app's shortcuts are keys to bind here, not commands.
                    Keys.onShortcutOverride: event => event.accepted = true
                    Keys.onPressed: event => {
                        event.accepted = true
                        if (!event.isAutoRepeat) keysDialog.bind(event.nativeScanCode & 0x1ff)
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.AllButtons
                        onPressed: mouse => {
                            const b = mouse.button === Qt.LeftButton ? 1 : mouse.button === Qt.MiddleButton ? 2
                                    : mouse.button === Qt.RightButton ? 3 : mouse.button === Qt.BackButton ? 4
                                    : mouse.button === Qt.ForwardButton ? 5 : 0
                            if (b > 0) keysDialog.bind(0x200 + b)
                        }
                        onWheel: wheel => {
                            if (wheel.angleDelta.y !== 0) keysDialog.bind(wheel.angleDelta.y > 0 ? 0x206 : 0x207)
                        }
                    }
                }
                VButton { text: qsTr("取消"); ghost: true; onClicked: keysDialog.listening = "" }
            }
        }
    }

    DialogSection { text: qsTr("按键") }
    DialogGroup {
        Repeater {
            model: veyra.ps5.keyBindings || []
            delegate: VRow {
                required property var modelData
                label: keysDialog.labels[modelData.id] || modelData.id
                RowLayout {
                    spacing: 4
                    Repeater {
                        model: modelData.inputs
                        delegate: VButton {
                            required property var modelData
                            text: modelData.name + "  ×"
                            ghost: true
                            tip: qsTr("点按解除这个按键")
                            maxTextWidth: 140
                            onClicked: veyra.ps5Set("bindKey", { code: modelData.code, action: "" })
                        }
                    }
                    VButton {
                        objectName: "ps5-keys-add-" + modelData.id
                        text: keysDialog.listening === modelData.id ? qsTr("等待按键…") : "+"
                        ghost: keysDialog.listening !== modelData.id
                        primary: keysDialog.listening === modelData.id
                        onClicked: { keysDialog.listening = modelData.id; listenBox.forceActiveFocus() }
                    }
                }
            }
        }
    }
    DialogNote {
        text: qsTr("Ctrl+Alt+Shift+Z 释放键盘和鼠标，这个组合不会按到手柄。GTA 6 在车上用扳机加速刹车：用「切换步行/载具」键切换，屏幕会提示当前模式。鼠标控制右摇杆，游戏里的手柄灵敏度和死区也会影响手感")
    }
}
