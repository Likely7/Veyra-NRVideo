import QtQuick
import QtTest
import Veyra

// 性能诊断 dialog against a scripted veyra.perfDiagnostics: start, recording,
// result with three findings, dismiss. Screenshots go to the working folder.
Item {
    id: root
    width: 720; height: 640
    QtObject {
        id: veyra
        property var perfDiagnostics: QtObject {
            property string state: "idle"
            property int elapsedSeconds: 0
            property int durationSeconds: 0
            property int marks: 0
            property string message: ""
            property var findings: []
            property string headline: ""
            property string folder: ""
            property string zipPath: ""
            property string outputRoot: "C:\\Users\\%USERNAME%\\Videos\\Veyra\\诊断"
            property bool analyzerAvailable: true
            property int startedMinutes: 0
            property int markCalls: 0
            property bool dismissed: false
            function start(minutes) { startedMinutes = minutes; state = "recording"; durationSeconds = minutes * 60; return true }
            function stop() { state = "analyzing" }
            function mark() { ++markCalls; ++marks }
            function openReport() { }
            function openFolder() { }
            function copyPath() { }
            function dismiss() { dismissed = true; state = "idle" }
        }
        function logUi(category, text) { }
    }
    PerfDiagnosticsDialog { id: dialog }

    function byText(item, text) {
        if (!item) return null
        if (item.text === text && item.visible) return item
        for (const child of item.children || []) {
            const found = byText(child, text)
            if (found) return found
        }
        return null
    }
    function save(name) {
        const image = testCase.grabImage(dialog.contentItem)
        image.save("perf-diagnostics-" + name + ".png")
    }

    TestCase {
        id: testCase
        name: "PerfDiagnostics"
        when: windowShown
        function test_flow() {
            const d = veyra.perfDiagnostics
            dialog.open()
            tryCompare(dialog, "opened", true)
            wait(300)
            const start = root.byText(dialog.contentItem, "开始诊断")
            verify(start !== null, "start button")
            verify(root.byText(dialog.contentItem, "5 分钟") !== null, "duration choices")
            root.save("idle")
            mouseClick(start, start.width / 2, start.height / 2)
            compare(d.startedMinutes, 5)
            tryCompare(dialog, "opened", false)

            d.elapsedSeconds = 65
            dialog.open()
            tryCompare(dialog, "opened", true)
            wait(300)
            verify(root.byText(dialog.contentItem, "记录中 01:05 / 05:00") !== null, "recording clock")
            const mark = root.byText(dialog.contentItem, "标记")
            verify(mark !== null)
            mouseClick(mark, mark.width / 2, mark.height / 2)
            compare(d.markCalls, 1)
            root.save("recording")

            d.state = "done"
            d.headline = "平均 59.8 fps · 最慢 1% 平均 57.1 fps · 卡顿 3 次"
            d.zipPath = "C:\\Users\\%USERNAME%\\Videos\\Veyra\\诊断\\Veyra诊断_2026-11-27_1530.zip"
            d.findings = [
                { id: "R1", severity: "high", title: "NVIDIA Smooth Motion 正作用于 Veyra", advice: "在 NVIDIA App 里对 Veyra 关闭 Smooth Motion。", evidence: "NvPresent64.dll 已加载" },
                { id: "R9", severity: "medium", title: "补帧超出 GPU 预算", advice: "降低补帧档位。", evidence: "补帧对被拒 126/771" },
                { id: "R14", severity: "info", title: "画面由系统合成（窗口模式）", advice: "全屏播放可减少约一帧延迟。", evidence: "" }
            ]
            wait(300)
            verify(root.byText(dialog.contentItem, "打开报告") !== null, "open report")
            verify(root.byText(dialog.contentItem, "补帧超出 GPU 预算") !== null, "finding shown")
            root.save("done")
            const done = root.byText(dialog.contentItem, "完成")
            mouseClick(done, done.width / 2, done.height / 2)
            verify(d.dismissed)
            tryCompare(dialog, "opened", false)
        }
    }
}
