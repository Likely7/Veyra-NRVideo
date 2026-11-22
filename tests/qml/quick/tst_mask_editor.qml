import QtQuick
import QtTest
import Veyra

Item {
    id: fixture
    width: 420; height: 440
    QtObject {
        id: backend
        property var protectionState: ({enabled:true,feather:2,regions:[],drawShape:""})
        property var exclusionState: ({enabled:true,feather:12,regions:[],drawShape:""})
        property var preferences: ({})
        property bool exclusionOverlayVisible: false
        property bool videoHdr: false
        property bool nrEnabled: false
        property var lastModeEdit: ({})
        property var edits: []
        function patch(all, fields) {
            if(all) exclusionState=Object.assign({},exclusionState,fields)
            else protectionState=Object.assign({},protectionState,fields)
        }
        function beginMaskDraw(all,shape) {
            patch(!all,{drawShape:""});patch(all,{drawShape:shape})
            edits=edits.concat([{all:all,shape:shape}])
        }
        function setMaskFeather(all,value) { patch(all,{feather:value}) }
        function selectExclusionRegion(index) { exclusionOverlayVisible=true;patch(true,{selected:index}) }
        function setExclusionRegionMode(index,mode) {
            lastModeEdit={index:index,mode:mode}
            patch(true,{regions:exclusionState.regions.map(region => region.index===index
                       ? Object.assign({},region,{mode:mode}) : region)})
        }
        function clearMaskRegions(all) { patch(all,{regions:[]}) }
        function removeMaskRegion(all,index) {
            const state=all?exclusionState:protectionState
            patch(all,{regions:state.regions.filter(region=>region.index!==index)})
        }
    }
    MaskEditor {
        id: editor
        property var veyra: backend
        width: fixture.width-32
        anchors.left: parent.left; anchors.leftMargin: 16
        namePrefix: "fixture-mask"
    }
    TestCase {
        name: "MaskEditor"
        when: windowShown
        function init() {
            fixture.width=420;editor.allEffects=false;backend.edits=[]
            backend.exclusionOverlayVisible=false
            backend.protectionState={enabled:true,feather:2,regions:[],drawShape:""}
            backend.exclusionState={enabled:true,feather:12,regions:[],drawShape:""}
            wait(30)
        }
        function test_draw_routes_to_the_selected_mask() {
            for(const all of [false,true]) {
                editor.allEffects=all
                const button=findChild(editor,"fixture-mask-draw-ellipse")
                mouseClick(button)
                compare(backend.edits[backend.edits.length-1].all,all)
                compare(editor.drawShape,"ellipse")
                mouseClick(button)
                compare(editor.drawShape,"")
                mouseClick(findChild(editor,"fixture-mask-draw-rect"))
                compare(editor.drawShape,"rect")
            }
        }
        function test_feather_does_not_lose_model_binding() {
            const slider=findChild(editor,"fixture-mask-feather")
            compare(slider.value,2)
            slider.commitValue(29)
            compare(backend.protectionState.feather,29)
            backend.patch(false,{feather:7})
            compare(slider.value,7)
            editor.allEffects=true
            compare(slider.value,12)
            slider.commitValue(41)
            compare(backend.exclusionState.feather,41)
            compare(backend.protectionState.feather,7)
        }
        function test_delete_uses_the_storage_index() {
            editor.allEffects=true
            backend.patch(true,{regions:[{index:3,left:.1,top:.2,right:.5,bottom:.6,ellipse:true}]})
            wait(30)
            const button=findChild(editor,"fixture-mask-remove-0")
            verify(button)
            mouseClick(button)
            compare(backend.exclusionState.regions.length,0)
        }
        function test_narrow_editor_buttons_remain_reachable() {
            fixture.width=320;wait(30)
            for(const name of ["rect","ellipse"]) {
                const button=findChild(editor,"fixture-mask-draw-"+name)
                const point=button.mapToItem(editor,0,0)
                verify(point.x>=0&&point.x+button.width<=editor.width+1)
            }
        }
        function test_region_mode_uses_storage_index_and_legacy_default() {
            fixture.width=320
            editor.allEffects=true
            backend.patch(true,{regions:[{index:3,left:.1,top:.2,right:.5,bottom:.6,ellipse:true}]})
            wait(30)
            const select=findChild(editor,"fixture-mask-mode-0")
            verify(select)
            compare(select.value,select.options[0].label)
            const point=select.mapToItem(editor,0,0)
            verify(point.x>=0&&point.x+select.width<=editor.width+1)
            select.picked("2")
            compare(backend.lastModeEdit.index,3)
            compare(backend.lastModeEdit.mode,2)
            tryCompare(findChild(editor,"fixture-mask-mode-0"),"value",select.options[2].label)
        }
        function test_outline_toggle_and_storage_index_selection() {
            editor.allEffects=true
            backend.patch(true,{regions:[{index:3,left:.1,top:.2,right:.5,bottom:.6,ellipse:false}]})
            wait(30)
            const toggle=findChild(editor,"fixture-mask-show-regions")
            mouseClick(toggle)
            compare(backend.exclusionOverlayVisible,true)
            mouseClick(toggle)
            compare(backend.exclusionOverlayVisible,false)
            mouseClick(findChild(editor,"fixture-mask-select-0"))
            compare(backend.exclusionState.selected,3)
            compare(backend.exclusionOverlayVisible,true)
            compare(backend.exclusionState.enabled,true)
        }
    }
}
