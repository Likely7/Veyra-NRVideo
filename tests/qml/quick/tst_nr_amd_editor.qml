// UI-only snapshot. This never claims a Radeon or a neural network was run.
import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0
Item {
    id: root
    width: 1100; height: 1400
    readonly property var defaults: ({index:1,runtime:4,sizePolicy:0,manualSize:true,sizePercent:37,
        performance:true,intensity:1,tone:1,structure:1,skin:-1,style:0,autoMask:true,uiCorrection:false,
        total:1,darken:1,brighten:1,color:1,luminance:1,temporal:false,antiFlicker:0,
        correctionEnabled:false,correctionAuto:true,correctionAmount:1})
    property var params: defaults
    property var edits: []
    NrLayerEditor {
        id: listEditor
        x:10; y:10; width:500
        layerData: root.params
        onEdited: (index,key,amount) => {const e=root.edits.slice();e.push({index:index,key:key,amount:amount});root.edits=e}
    }
    NrLayerEditor {
        id: nodeEditor
        x:560; y:10; width:500
        listControls:false
        layerData:root.params
        onEdited: (index,key,amount) => {const e=root.edits.slice();e.push({index:index,key:key,amount:amount});root.edits=e}
    }
    TestCase {
        name: "AmdNrEditorSnapshot"
        when: windowShown
        function init(){root.params=Object.assign({},root.defaults);root.edits=[]}
        function control(editor,name){const child=findChild(editor,name);verify(child!==null,name);return child}
        function test_both_modes_use_actual_provider_label(){
            compare(listEditor.amdNr,true);compare(nodeEditor.amdNr,true)
            verify(listEditor.amdRuntimeLabel.indexOf("RX 7000")>=0)
            compare(nodeEditor.amdRuntimeLabel,listEditor.amdRuntimeLabel)
            compare(control(listEditor,"nr-performance").checked,true)
            compare(control(nodeEditor,"nr-performance").checked,true)
        }
        function test_small_original_and_percent_are_independent(){
            for(const editor of [listEditor,nodeEditor]){
                const sw=control(editor,"nr-performance")
                mouseClick(sw,sw.width/2,sw.height/2)
                compare(root.edits[root.edits.length-1].key,"performance")
                compare(root.edits[root.edits.length-1].amount,0)
                compare(control(editor,"nr-size-percent").value,37)
            }
        }
        function test_saved_original_and_percentage_restore_in_both_modes(){
            root.params=Object.assign({},root.defaults,{performance:false,sizePercent:62})
            for(const editor of [listEditor,nodeEditor]){
                compare(control(editor,"nr-performance").checked,false)
                compare(control(editor,"nr-size-percent").value,62)
                compare(control(editor,"nr-manual-size-row").visible,true)
            }
        }
        function test_list_buttons_follow_editor_owner(){
            compare(control(listEditor,"nr-copy").visible,true)
            compare(control(nodeEditor,"nr-copy").visible,false)
        }
    }
}
