// SPDX-License-Identifier: GPL-3.0-only
import QtQuick

QtObject {
    property bool xbox: false
    readonly property var names: [
        xbox ? "A" : "×", xbox ? "B" : "○", xbox ? "X" : "□", xbox ? "Y" : "△",
        qsTr("方向键 左"), qsTr("方向键 右"), qsTr("方向键 上"), qsTr("方向键 下"),
        xbox ? "LB" : "L1", xbox ? "RB" : "R1", xbox ? "LS" : "L3", xbox ? "RS" : "R3",
        xbox ? "Menu" : qsTr("选项键"), xbox ? "View" : qsTr("创建键"),
        qsTr("触摸板按下"), xbox ? "Xbox" : "PS", xbox ? "LT" : "L2", xbox ? "RT" : "R2",
        qsTr("左摇杆 上"), qsTr("左摇杆 下"), qsTr("左摇杆 左"), qsTr("左摇杆 右"),
        qsTr("右摇杆 上"), qsTr("右摇杆 下"), qsTr("右摇杆 左"), qsTr("右摇杆 右"),
        qsTr("慢走"), qsTr("菜单摇杆"), qsTr("触摸板上滑"), qsTr("触摸板下滑"),
        qsTr("触摸板左滑"), qsTr("触摸板右滑"), xbox ? "LS + RS" : "L3 + R3",
        qsTr("半油门／刹车")
    ]

    function groups(driving) {
        return [
            { title: qsTr("移动"), actions: driving ? [18, 19, 26, 10] : [18, 19, 20, 21, 26, 10] },
            { title: qsTr("视角"), actions: [22, 23, 24, 25] },
            { title: qsTr("战斗"), actions: driving ? [8, 9, 11, 32] : [17, 16, 8, 9, 11, 32] },
            { title: qsTr("交互"), actions: [0, 1, 2, 3] },
            { title: qsTr("菜单"), actions: xbox ? [4, 5, 6, 7, 12, 13, 15, 27] : [4, 5, 6, 7, 12, 13, 14, 15, 27, 28, 29, 30, 31] },
            { title: qsTr("驾驶"), actions: driving ? [17, 16, 20, 21, 33] : [] }
        ]
    }

    function binding(input, driving, action, slot = 0) {
        const keys = slot === 0 ? (driving ? input?.drivingBindings : input?.bindings)
            : (driving ? input?.drivingAlternateBindings : input?.alternateBindings)
        return keys?.[action] ?? "—"
    }

    function actionNote(input, driving, action) {
        return (driving ? input?.drivingActionNotes : input?.actionNotes)?.[action] ?? ""
    }

    function pressed(live, action) {
        if (!live) return false
        if (action === 32) return pressed(live, 10) && pressed(live, 11)
        if (action < 0 || action >= 26) return false
        if (action < 16) return (Number(live.buttons || 0) & (1 << action)) !== 0
        if (action === 16 || action === 17) return Number(action === 16 ? live.l2 : live.r2) > 0
        const axes = [live.leftY, live.leftX, live.rightY, live.rightX]
        return Number(axes[Math.floor((action - 18) / 2)] || 0) * (action % 2 === 0 ? -1 : 1) > 0.3
    }

    function toggles(input, action, driving) {
        if (!input?.bindings || (driving && action === 16)) return false
        return [1, 10, 16, 27].includes(action) && (input.toggleAction?.(action) ?? false)
    }
}
