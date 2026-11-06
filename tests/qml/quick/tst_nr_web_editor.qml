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
        highlightProtection:1,localCompression:0.75,temporalStability:0.8,
        correctionEffective:{colorRetention:0.9,luminanceRetention:0.25,shadowProtection:0.55,
            highlightProtection:1,localCompression:1,temporalStability:1}})
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
        name:"NrWebEditor"
        when:windowShown
        function initTestCase(){Theme.reduced=true;wait(100);verify(waitForPolish(root.Window.window))}
        function init(){veyra.setPreference("nrWebEditor",false);root.params=Object.assign({},root.defaults);root.edits=[];wait(20)}
        function cleanup(){veyra.setPreference("nrWebEditor",false)}
        function control(parent,name){const item=findChild(parent,name);verify(item!==null,name);return item}
        function open(){veyra.setPreference("nrWebEditor",true);wait(20);verify(list.webEnabled);verify(nodes.webEnabled)}
        function web(owner){return control(owner,"nr-web-editor").item}
        function test_default_and_switch_share_one_model(){
            verify(!list.webEnabled);verify(control(list,"nr-slider-editor").enabled)
            const before=JSON.stringify(root.params),toggle=control(list,"nr-web-toggle")
            mouseClick(toggle,toggle.width/2,toggle.height/2);wait(20)
            verify(list.webEnabled&&nodes.webEnabled)
            verify(!control(list,"nr-slider-editor").enabled&&!control(nodes,"nr-slider-editor").enabled)
            compare(JSON.stringify(root.params),before)
            control(list,"nr-slider-editor").edited(1,"tone",0.1);compare(root.edits.length,0)
            control(web(list),"nr-web-tone").moved(0.35)
            compare(root.params.tone,0.35);compare(control(nodes,"nr-tone").value,0.35)
            veyra.setPreference("nrWebEditor",false);wait(20)
            verify(control(list,"nr-slider-editor").enabled);compare(control(list,"nr-tone").value,0.35)
        }
        function test_all_parameters_remain_available_in_web_panel(){
            open();const w=web(list)
            const sliderKeys=["intensity","tone","structure","total","size-percent","hueProtection","chromaProtection","neutralProtection","correctionAmount"]
            for(const key of sliderKeys)control(w,"nr-web-"+key)
            for(const key of ["runtime","performance","motion-source","resolution","skin-mode","style","autoMask","uiCorrection","temporal","antiflicker","copy","reset","remove","use-auto-defaults"])
                control(w,"nr-web-"+key)
            const changes=control(w,"nr-web-changes"),protections=control(w,"nr-web-protections")
            compare(changes.axes.map(a=>a.key).join(","),"darken,brighten,color,luminance")
            compare(protections.axes.map(a=>a.key).join(","),"colorRetention,luminanceRetention,shadowProtection,highlightProtection,localCompression,temporalStability")
            compare(control(w,"nr-web-size-percent").value,37)
            control(w,"nr-web-size-percent").moved(62);compare(root.params["sizePercent"],62)
            verify(control(w,"nr-web-model-group").expanded)
            verify(control(w,"nr-web-correction-group").expanded)
            verify(control(w,"nr-web-experimental-group").expanded)
            compare(control(web(nodes),"nr-web-copy").visible,false)
        }
        function test_auto_displays_effective_values_and_keeps_manual_values(){
            root.params=Object.assign({},root.defaults,{correctionAuto:true});open()
            const p=control(web(list),"nr-web-protections")
            verify(!p.editable);fuzzyCompare(p.valueAt(0),0.9,0.00001)
            control(web(list),"nr-web-correctionAuto").picked("0");wait(20)
            verify(p.editable);fuzzyCompare(p.valueAt(0),0.1,0.00001)
            p.setSelected(0.6);compare(root.params.colorRetention,0.6)
            control(web(list),"nr-web-correctionAuto").picked("1");wait(20)
            compare(root.params.colorRetention,0.6);fuzzyCompare(p.valueAt(0),0.9,0.00001)
        }
        function test_amd_keeps_original_model_and_size_restrictions(){
            root.params=Object.assign({},root.defaults,{runtime:4});open()
            verify(!control(web(list),"nr-web-model-group").enabled)
            compare(control(web(list),"nr-web-performance").checked,true)
            const select=control(web(list),"nr-web-resolution")
            verify(select.options.find(o=>o.id==="1").disabled)
            verify(select.options.find(o=>o.id==="5").disabled)
        }
        function test_missing_runtime_still_allows_residual_preset_editing(){
            const saved=veyra.effectCapabilities
            try {
                veyra.effectCapabilities={nr2:{available:false,reason:"Missing runtime fixture"}}
                compare(control(list,"nr-darken").enabled,true)
                open()
                verify(control(web(list),"nr-web-changes").enabled)
                verify(control(web(list),"nr-web-protections").enabled)
                verify(!control(web(list),"nr-web-model-group").enabled)
                control(web(list),"nr-web-changes").setSelected(0.4)
                compare(root.params.darken,0.4)
            } finally { veyra.effectCapabilities=saved }
        }
        function test_switch_off_during_drag_stops_future_writes(){
            open();const p=control(web(list),"nr-web-changes"),s=control(p,"nr-web-changes-surface")
            const point=p.pointAt(0,0.5),next=p.pointAt(0,0.8)
            mousePress(s,point.x,point.y);mouseMove(s,next.x,next.y)
            veyra.setPreference("nrWebEditor",false);root.edits=[];wait(80)
            mouseRelease(list,10,10);compare(root.edits.length,0)
        }
    }
}
