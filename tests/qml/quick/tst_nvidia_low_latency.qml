// NVIDIA low latency choice and its hint. UI only: no driver call is made.
// Field 2026-11-02: On + Boost dropped generated frames under heavy load, so
// the hint recommends On there.
import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0
Item {
    id: root
    width: 640; height: 240
    NvidiaLowLatencyEditor { id: editor; x: 10; y: 10; width: 560; namePrefix: "test" }
    TestCase {
        name: "NvidiaLowLatencyHint"
        when: windowShown
        function init(){veyra.nvidiaLowLatencyAvailable=true;veyra.nvidiaLowLatency=1}
        function cleanup(){veyra.nvidiaLowLatencyAvailable=false;veyra.nvidiaLowLatency=0}
        function control(name){const child=findChild(editor,name);verify(child!==null,name);return child}
        function shownText(item,text){
            if(item.text===text&&item.visible&&item.height>0)return item
            for(let i=0;i<item.children.length;++i){const found=shownText(item.children[i],text);if(found)return found}
            return null
        }
        function test_hint_recommends_on_under_heavy_load(){
            const row=control("test-nvidia-low-latency-row")
            verify(row.hint.indexOf("高负载时建议选「开」而不是「开＋增强」")>=0,row.hint)
            verify(row.hint.indexOf("掉帧")>=0,row.hint)
            // The hint quotes the labels the choice really shows.
            const labels=control("test-nvidia-low-latency").options.map(o=>o.label)
            compare(labels.length,3)
            verify(row.hint.indexOf("「"+labels[1]+"」")>=0&&row.hint.indexOf("「"+labels[2]+"」")>=0,row.hint)
        }
        function test_hint_is_shown_below_the_choice(){
            const row=control("test-nvidia-low-latency-row")
            verify(row.hintBelow)
            const text=shownText(row,row.hint)
            verify(text!==null,"hint text visible")
            const choice=control("test-nvidia-low-latency")
            verify(text.mapToItem(row,0,0).y>=choice.mapToItem(row,0,0).y+choice.height,"hint sits below the choice")
        }
        function test_unavailable_keeps_its_own_hint(){
            veyra.nvidiaLowLatencyAvailable=false
            const row=control("test-nvidia-low-latency-row")
            compare(row.hint,"需要 NVIDIA 显卡")
            compare(control("test-nvidia-low-latency").enabled,false)
        }
    }
}
