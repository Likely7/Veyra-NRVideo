import QtQuick
import QtQuick.Controls
import QtTest
import Veyra

Item {
    width: 900; height: 650
    CaptureLegacyPicker { id: picker }
    SignalSpy { id: chosen; target: picker; signalName: "selected" }
    TestCase {
        name: "CaptureLegacyPicker"
        when: windowShown
        function cleanup() { picker.close(); chosen.clear() }
        function test_full_list_search_and_selection() {
            let entries = []
            for (let i = 0; i < 16000; ++i)
                entries.push({id: "exact:" + i, label: "1920 x 1080 @ 60 fps · " + (i % 2 ? "NV12" : "YUY2") + " [format " + i + "]"})
            picker.formats = entries
            picker.open()
            tryCompare(picker, "opened", true)
            const list = findChild(picker.contentItem, "capture-legacy-list")
            const search = findChild(picker.contentItem, "capture-legacy-search")
            tryCompare(list, "count", 16000)
            verify(list.contentItem.children.length < 100, "Rows must stay virtualized")
            search.text = "1920 60 NV12"
            picker.applySearch()
            tryCompare(list, "count", 8000)
            tryVerify(() => list.itemAtIndex(0) !== null)
            list.itemAtIndex(0).clicked()
            compare(chosen.count, 1)
            compare(chosen.signalArguments[0][0], "exact:1")
            tryCompare(picker, "visible", false)
            picker.open()
            tryCompare(picker, "opened", true)
            tryCompare(list, "count", 16000)
        }
        function test_unsupported_rows_and_empty_search() {
            picker.formats = [{id:"unsupported",label:"Unsupported",disabled:true}]
            picker.open()
            tryCompare(picker, "opened", true)
            const list = findChild(picker.contentItem, "capture-legacy-list")
            tryVerify(() => list.itemAtIndex(0) !== null)
            compare(list.itemAtIndex(0).enabled, false)
            findChild(picker.contentItem, "capture-legacy-search").text = "not-found"
            picker.applySearch()
            compare(list.count, 0)
            compare(chosen.count, 0)
        }
        function test_mjpeg_keeps_red_warning() {
            picker.formats = [{id:"mjpeg",label:"MJPEG（不建议使用）",danger:true,costHint:true}]
            picker.open()
            tryCompare(picker, "opened", true)
            const list = findChild(picker.contentItem, "capture-legacy-list")
            tryVerify(() => list.itemAtIndex(0) !== null)
            compare(list.itemAtIndex(0).contentItem.color, Theme.err)
            verify(list.itemAtIndex(0).contentItem.text.indexOf("不建议使用") >= 0)
        }
    }
}
