import QtQuick
import QtQuick.Layouts
import QtTest
import Veyra

Item {
    id: fixture
    width: 420;height: 400
    QtObject { id: fakeBridge; property int contentRate: 0 }
    ColumnLayout {
        anchors.left: parent.left;anchors.right: parent.right
        DedupModeRow { id: list; backend: fakeBridge; namePrefix: "fixture-list" }
        DedupModeRow { id: node; backend: fakeBridge; namePrefix: "fixture-node"; showHelp: false }
    }
    TestCase {
        name: "DedupModes"
        when: windowShown
        function init() { fakeBridge.contentRate=0;list.lastMode=1;node.lastMode=1;fixture.width=420;wait(30) }
        function selectRow(prefix) { return findChild(fixture,"fixture-"+prefix+"-content-rate") }
        function toggle(prefix) { mouseClick(findChild(fixture,"fixture-"+prefix+"-dedup-enabled")) }
        function test_only_two_modes_are_offered() {
            for(const editor of [list,node]) {
                compare(editor.label,"去除重复帧")
                compare(editor.modes.length,2)
                compare(editor.modes[0].id,"5");compare(editor.modes[0].label,"60→30")
                compare(editor.modes[1].id,"1");compare(editor.modes[1].label,"自动去除")
            }
        }
        function test_default_stays_off_until_enabled() {
            compare(fakeBridge.contentRate,0)
            compare(selectRow("list").enabled,false)
            toggle("list")
            compare(fakeBridge.contentRate,1)
            compare(selectRow("node").enabled,true)
        }
        function test_sparse_mode_id_and_external_updates() {
            fakeBridge.contentRate=1
            selectRow("list").picked("5")
            compare(fakeBridge.contentRate,5)
            compare(selectRow("list").value,"60→30")
            compare(selectRow("node").value,"60→30")
            fakeBridge.contentRate=1
            compare(selectRow("list").value,"自动去除")
            selectRow("list").picked("3")
            compare(fakeBridge.contentRate,1)
        }
        function test_turning_off_and_on_keeps_the_chosen_mode() {
            fakeBridge.contentRate=5
            toggle("list");compare(fakeBridge.contentRate,0)
            compare(selectRow("node").enabled,false)
            toggle("node");compare(fakeBridge.contentRate,5)
        }
        function test_legacy_label_and_narrow_layout() {
            for(const legacy of [2,3,4]){
                fakeBridge.contentRate=legacy
                compare(selectRow("list").value,"自动去除")
            }
            fixture.width=240;wait(50)
            const control=selectRow("node"),point=control.mapToItem(node,0,0)
            verify(point.x>=0&&point.x+control.width<=node.width+1)
        }
    }
}
