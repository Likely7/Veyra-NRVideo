import QtQuick
import QtTest
import Veyra
Item {
    width: 800; height: 600
    QtObject {
        id: mock
        property var ps5: ({canEndStream:true,canStandby:true,ending:false,endMessage:""})
        property int calls: 0
        property bool standby: false
        property bool accept: true
        function ps5EndStream(value) { ++calls;standby=value;return accept }
    }
    Item {
        id: host; anchors.fill: parent
        property string dialog: ""
        property real motionScale: 1
        function close() { dialog="" }
    }
    Ps5StopDialog { id: panel; dialogHost: host; backend: mock }
    Ps5StreamStopButton { id: entry; backend: mock; onRequestStop: host.dialog="ps5-stop" }
    TestCase {
        name: "Ps5StopFlow"; when: windowShown
        function init() { Theme.reduced=true;host.dialog="";mock.ps5={canEndStream:true,canStandby:true,ending:false,endMessage:""};mock.calls=0;mock.standby=false;mock.accept=true }
        function test_entry_opens_choice_without_power_command() {
            verify(entry.visible);mouseClick(entry);verify(panel.shown);compare(mock.calls,0)
        }
        function test_only_disconnect_does_not_request_standby() {
            host.dialog="ps5-stop";mouseClick(findChild(panel,"ps5-stop-keep-on"));compare(mock.calls,1);verify(!mock.standby);verify(!panel.shown)
        }
        function test_standby_requires_explicit_choice() {
            host.dialog="ps5-stop";mouseClick(findChild(panel,"ps5-stop-standby"));compare(mock.calls,1);verify(mock.standby);verify(!panel.shown)
        }
        function test_cancel_keeps_stream_untouched() {
            host.dialog="ps5-stop";mouseClick(findChild(panel,"ps5-stop-cancel"));compare(mock.calls,0);verify(!panel.shown)
        }
        function test_pending_connection_disables_standby_and_rejection_stays_visible() {
            mock.ps5={canEndStream:true,canStandby:false,ending:false};host.dialog="ps5-stop"
            verify(!findChild(panel,"ps5-stop-standby").enabled);mock.accept=false
            mouseClick(findChild(panel,"ps5-stop-keep-on"));verify(panel.shown)
        }
        function test_inactive_hides_entry() { mock.ps5={canEndStream:false,canStandby:false};verify(!entry.visible) }
    }
}
