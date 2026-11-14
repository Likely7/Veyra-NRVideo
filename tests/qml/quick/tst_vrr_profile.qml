import QtQuick
import QtQuick.Layouts
import QtTest
import Veyra

Item {
    id: fixture
    width: 400; height: 360
    QtObject {
        id: profile
        property bool busy: false
        property bool needsRepair: true
        property bool hasBackup: false
        readonly property bool canRepair: !busy && needsRepair
        readonly property bool canRestore: !busy && hasBackup
        property string summary: qsTr("尚未检查")
        property string detail: typeof vrrProfileTestHint !== "undefined" ? vrrProfileTestHint
            : qsTr("仅为当前软件路径配置窗口和全屏 G-SYNC，可恢复原设置；显示器和驱动的 G-SYNC 总开关仍需开启。")
        property int checks: 0
        property int repairs: 0
        property int restores: 0
        function check() { checks++; summary = "已检查" }
        function repair() { repairs++; needsRepair = false; hasBackup = true; summary = "已允许窗口和全屏" }
        function restore() { restores++; needsRepair = true; hasBackup = false }
    }
    ColumnLayout {
        width: parent.width
        NvidiaVrrProfileRow { id: row; backend: profile }
    }
    TestCase {
        name: "NvidiaVrrProfileSetup"
        when: windowShown
        function button(name) { return findChild(row, "presentation-gsync-" + name) }
        function initTestCase() {
            compare(profile.checks, 0); compare(profile.repairs, 0); compare(profile.restores, 0)
        }
        function init() {
            row.backend = profile
            fixture.width = 400
            profile.busy = false; profile.needsRepair = true; profile.hasBackup = false
            profile.checks = 0; profile.repairs = 0; profile.restores = 0
            profile.summary = qsTr("尚未检查")
            wait(20)
        }
        function test_no_automatic_driver_calls() {
            compare(profile.checks, 0); compare(profile.repairs, 0); compare(profile.restores, 0)
        }
        function test_explicit_check_repair_restore() {
            mouseClick(button("check")); compare(profile.checks, 1)
            compare(profile.repairs, 0)
            verify(button("repair").visible); verify(!button("restore").visible)
            mouseClick(button("repair")); compare(profile.repairs, 1)
            verify(!button("repair").visible); verify(button("restore").visible)
            mouseClick(button("restore")); compare(profile.restores, 1)
        }
        function test_busy_prevents_repeated_actions() {
            profile.busy = true
            verify(!button("check").enabled); verify(!button("repair").visible)
            profile.hasBackup = true; verify(!button("restore").visible)
        }
        function test_missing_controller_hides_row() {
            row.backend = null; verify(!row.visible)
        }
        function test_narrow_layout_data() { return [{tag:"280",width:280},{tag:"360",width:360},{tag:"520",width:520}] }
        function test_narrow_layout(data) {
            fixture.width = data.width
            profile.hasBackup = true
            wait(20)
            for (const name of ["check", "repair", "restore"]) {
                const item = button(name)
                const point = item.mapToItem(row, 0, 0)
                verify(point.x >= -1 && point.x + item.width <= row.width + 1,
                    name + " remains within the panel: x=" + point.x + " width=" + item.width + " panel=" + row.width)
            }
            verify(row.implicitHeight > 40, "the explanatory text contributes to row height")
        }
    }
}
