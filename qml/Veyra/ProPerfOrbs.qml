// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

RowLayout {
    property int size: 56
    property bool showLabels: true
    spacing: 10
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
        fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
        value: veyra.chainTotalMsKnown ? veyra.chainTotalMs.toFixed(1) + "ms" : "—"
        label: qsTr("处理耗时")
        tip: qsTr("增强链总耗时（最近一秒平均，与 1.4.4 相同口径），环 = 占源帧预算") + (veyra.stageBudgetMs > 0 ? " " + veyra.stageBudgetMs.toFixed(1) + " ms" : "") + qsTr(" 的比例")
    }
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.gpuUtilizationKnown
        fraction: veyra.gpuUtilization / 100
        value: known ? Math.round(veyra.gpuUtilization) + "%" : "—"
        label: qsTr("GPU 占用")
        tip: qsTr("Windows GPU 引擎占用率（") + (veyra.gpuMonitorName || qsTr("全部显卡")) + qsTr("，最忙的引擎类型，全系统），每秒采样；在 设置 → 通用与外观 中更换监控的显卡")
    }
    // How much of one source frame's time the enhancement chain uses (the plan's
    // "负载预算 = 耗时 / 预算"). It used to show output fps / target fps, which
    // reads 100% whenever the output keeps up, however light the load.
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
        fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
        value: known ? Math.round(veyra.chainTotalMs / veyra.stageBudgetMs * 100) + "%" : "—"
        label: qsTr("负载预算")
        tip: qsTr("增强链 GPU 耗时（最近一秒平均）占一个源帧时间（") + veyra.stageBudgetMs.toFixed(1)
             + qsTr(" ms）的比例；超过 100% 就跟不上源帧率。当前状态：") + veyra.runStatus
    }
}
