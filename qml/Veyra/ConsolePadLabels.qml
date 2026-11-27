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
        qsTr("触摸板左滑"), qsTr("触摸板右滑"), xbox ? "LS + RS" : "L3 + R3"
    ]
}
