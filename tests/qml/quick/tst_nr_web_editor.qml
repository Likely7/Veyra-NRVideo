import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

Item {
    id: root
    width: 1000; height: 2200
    readonly property var defaults: ({index:1,runtime:2,sizePolicy:0,manualSize:true,sizePercent:37,
        performance:true,intensity:1,tone:1,structure:1,skin:-1,style:0,autoMask:true,uiCorrection:false,
        total:1,darken:1,brighten:1,color:1,luminance:1,temporal:false,antiFlicker:2,
        correctionEnabled:true,correctionAuto:false,correctionAmount:1,hueProtection:1,chromaProtection:0.75,
        neutralProtection:0,colorRetention:0.1,luminanceRetention:0.2,shadowProtection:0.3,
        highlightProtection:1,localCompression:0.75,temporalStability:0.8})
    property var params: defaults
    property var edits: []
    function apply(index,key,value) {
        const p=Object.assign({},params)
        if(["performance","temporal","autoMask","uiCorrection","correctionEnabled","correctionAuto"].indexOf(key)>=0)p[key]=value!==0
        else p[key]=value
        params=p;edits=edits.concat([{index:index,key:key,value:value}])
    }
    NrLayerEditor { id: list; width:440; layerData:root.params; onEdited:(i,k,v)=>root.apply(i,k,v) }
    NrLayerEditor { id: nodes; x:500; width:440; layerData:root.params; listControls:false; onEdited:(i,k,v)=>root.apply(i,k,v) }
    TestCase {
        name:"NrInlineEditor"
        when:windowShown
        function initTestCase(){Theme.reduced=true;wait(100);verify(waitForPolish(root.Window.window))}
        function control(owner,name){const item=findChild(owner,name);verify(item!==null,name);return item}
        function init(){
            veyra.setPreference("nrResidualView","list");veyra.setPreference("nrCorrectionView","list")
            root.params=Object.assign({},root.defaults);root.edits=[]
            for(const owner of [list,nodes])for(const name of ["nr-residual-group","nr-correction-group","nr-model-group","nr-experimental-group"])control(owner,name).expanded=true
            wait(20)
        }
        function cleanup(){veyra.setPreference("nrResidualView","list");veyra.setPreference("nrCorrectionView","list")}
        function test_no_global_block_or_duplicate_parameter_editors(){
            compare(findChild(list,"nr-web-toggle"),null);compare(findChild(list,"nr-slider-editor"),null)
            verify(control(list,"nr-runtime").enabled);verify(control(list,"nr-tone").enabled)
            verify(!list.residualWeb&&!list.correctionWeb)
        }
        function test_group_views_are_independent_and_shared_across_pages(){
            const before=JSON.stringify(root.params)
            control(list,"nr-residual-view").picked("web");wait(20)
            verify(list.residualWeb&&nodes.residualWeb);verify(!list.correctionWeb&&!nodes.correctionWeb)
            verify(control(list,"nr-residual-web").visible);verify(!control(list,"nr-darken").visible)
            verify(control(list,"nr-total").visible&&control(list,"nr-tone").enabled)
            control(nodes,"nr-correction-view").picked("web");wait(20)
            verify(list.correctionWeb&&nodes.correctionWeb)
            compare(JSON.stringify(root.params),before)
            control(list,"nr-residual-view").picked("list");wait(20)
            verify(list.correctionWeb&&nodes.correctionWeb);verify(control(list,"nr-darken").visible)
        }
        function test_all_nine_manual_parameters_roundtrip(){
            control(list,"nr-correction-view").picked("web");wait(20)
            const pad=control(list,"nr-correction-web")
            compare(pad.axes.length,9)
            compare(pad.axes.map(a=>a.key).join(","),"hueProtection,chromaProtection,neutralProtection,colorRetention,luminanceRetention,shadowProtection,highlightProtection,localCompression,temporalStability")
            for(let i=0;i<9;++i){pad.selectAxis(i);pad.setSelected((i+1)/10)}
            control(list,"nr-correction-view").picked("list");wait(20)
            for(let i=0;i<9;++i){const s=control(list,"nr-"+pad.axes[i].key);verify(s.visible&&s.enabled);fuzzyCompare(s.value,(i+1)/10,0.00001)}
            control(list,"nr-hueProtection").moved(0.44)
            control(list,"nr-correction-view").picked("web");wait(20)
            fuzzyCompare(pad.valueAt(0),0.44,0.00001)
        }
        function test_residual_master_stays_a_slider_and_hidden_inputs_do_not_write(){
            control(list,"nr-residual-view").picked("web");wait(20)
            const p=control(list,"nr-residual-web")
            compare(p.axes.length,4);p.selectAxis(2);p.setSelected(0.55)
            control(list,"nr-total").moved(2.5);compare(root.params.total,2.5)
            control(list,"nr-color").moved(1.5);compare(root.params.color,0.55)
            control(list,"nr-residual-view").picked("list");wait(20)
            p.edited("color",0.9);compare(root.params.color,0.55)
            control(list,"nr-color").moved(0.75);compare(root.params.color,0.75)
        }
        function test_auto_hides_manual_editor_without_losing_values_or_view(){
            control(list,"nr-correction-view").picked("web");wait(20)
            const p=control(list,"nr-correction-web");p.selectAxis(0);p.setSelected(0.35)
            control(list,"nr-correctionAuto").picked("1");wait(20)
            verify(!p.visible&&!control(list,"nr-correction-view").visible)
            verify(control(list,"nr-correctionAmount").visible)
            control(list,"nr-correctionAmount").moved(0.7)
            control(list,"nr-correctionAuto").picked("0");wait(20)
            verify(p.visible);compare(root.params.hueProtection,0.35);compare(root.params.correctionAmount,0.7)
        }
        function test_other_controls_and_amd_rules_stay_in_place(){
            for(const key of ["runtime","performance","motion-source","resolution","skin-mode","style","autoMask","uiCorrection","temporal","antiflicker","copy","reset","remove","use-auto-defaults"])control(list,"nr-"+key)
            control(list,"nr-size-percent").moved(62);compare(root.params.sizePercent,62)
            root.params=Object.assign({},root.defaults,{runtime:4})
            control(list,"nr-residual-view").picked("web");control(list,"nr-correction-view").picked("web");wait(20)
            verify(!control(list,"nr-model-group").enabled)
            verify(control(list,"nr-resolution").options.find(o=>o.id==="1").disabled)
            verify(control(list,"nr-resolution").options.find(o=>o.id==="5").disabled)
            compare(control(nodes,"nr-copy").visible,false)
        }
        function test_missing_runtime_keeps_residual_preset_editing(){
            const saved=veyra.effectCapabilities
            try{
                veyra.effectCapabilities={nr2:{available:false,reason:"Missing runtime"}}
                control(list,"nr-residual-view").picked("web");control(list,"nr-correction-view").picked("web");wait(20)
                verify(control(list,"nr-residual-web").enabled);verify(control(list,"nr-correction-web").enabled)
                verify(!control(list,"nr-model-group").enabled)
            }finally{veyra.effectCapabilities=saved}
        }
        function test_switch_view_during_drag_stops_late_writes(){
            control(list,"nr-residual-view").picked("web");wait(20)
            const p=control(list,"nr-residual-web"),s=control(p,"nr-residual-web-surface"),a=p.pointAt(0,0.5),b=p.pointAt(0,0.8)
            mousePress(s,a.x,a.y);mouseMove(s,b.x,b.y)
            control(list,"nr-residual-view").picked("list");root.edits=[];wait(80)
            mouseRelease(list,10,10);compare(root.edits.length,0)
        }
        function test_old_global_preference_only_migrates_view_choice(){
            const saved=veyra.preferences
            try{
                const prefs=Object.assign({},saved,{nrWebEditor:true});delete prefs.nrResidualView;delete prefs.nrCorrectionView;veyra.preferences=prefs;wait(20)
                verify(list.residualWeb&&list.correctionWeb);verify(control(list,"nr-runtime").enabled)
                control(list,"nr-residual-view").picked("list");wait(20)
                verify(!list.residualWeb&&list.correctionWeb)
            }finally{veyra.preferences=saved}
        }
    }
}
