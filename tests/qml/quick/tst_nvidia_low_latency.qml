import QtQuick
import QtQuick.Layouts
import QtTest
import Veyra
import VeyraTest 1.0
Item {
    id: fixture
    width: 640; height: 300
    QtObject {
        id: bridge
        property bool nvidiaLowLatencyAvailable: true
        property int nvidiaLowLatency: 0
        property bool running: false
        property string presentationStatus: ""
    }
    ColumnLayout {
        width: parent.width
        NvidiaLowLatencyEditor { id: list; backend: bridge; namePrefix: "test-list" }
        NvidiaLowLatencyEditor { id: node; backend: bridge; namePrefix: "test-node" }
    }
    TestCase {
        name: "NvidiaLowLatencyModes"
        when: windowShown
        function init(){bridge.nvidiaLowLatencyAvailable=true;bridge.nvidiaLowLatency=0;bridge.running=false;wait(20)}
        function choice(editor){return findChild(editor,editor.namePrefix+"-nvidia-low-latency")}
        function test_only_off_and_on(){
            for(const editor of [list,node]){
                const control=choice(editor)
                compare(control.options.length,2)
                compare(control.options[0].id,"0");compare(control.options[0].label,"关")
                compare(control.options[1].id,"1");compare(control.options[1].label,"开")
                compare(control.current,"0")
                compare(findChild(editor,editor.namePrefix+"-nvidia-low-latency-row").hint,"")
            }
            compare(bridge.nvidiaLowLatency,0,"creation does not enable low latency")
        }
        function test_clicks_and_shared_value(){
            const a=choice(list),b=choice(node)
            const on=a.currentItem.parent.children[1]
            mouseClick(on)
            compare(bridge.nvidiaLowLatency,1);compare(a.current,"1");compare(b.current,"1")
            const off=b.currentItem.parent.children[0]
            mouseClick(off)
            compare(bridge.nvidiaLowLatency,0);compare(a.current,"0");compare(b.current,"0")
        }
        function test_legacy_value_has_no_third_selection(){
            bridge.nvidiaLowLatency=2
            for(const editor of [list,node]){compare(choice(editor).current,"1");compare(choice(editor).currentIndex,1)}
        }
        function test_unavailable_keeps_choice_disabled(){
            bridge.nvidiaLowLatency=1;bridge.nvidiaLowLatencyAvailable=false
            for(const editor of [list,node]){
                compare(choice(editor).enabled,false);compare(choice(editor).current,"0")
                compare(findChild(editor,editor.namePrefix+"-nvidia-low-latency-row").hint,"需要 NVIDIA 显卡")
            }
            compare(bridge.nvidiaLowLatency,1,"hardware visibility does not rewrite the retained setting")
        }
    }
}
