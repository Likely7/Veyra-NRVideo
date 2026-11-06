import QtQuick
Item {
    id: test
    property string media: ""
    property string shot: ""
    property bool saved: false
    function find(item,name) {
        if(item.objectName===name)return item
        for(const child of item.children){const hit=find(child,name);if(hit)return hit}
        return null
    }
    function card(){return find(test.parent.parent,"nr-card-1")}
    function control(name){const item=find(card(),name);if(!item)throw new Error("Missing "+name);return item}
    function check(ok,label){if(!ok)throw new Error(label);veyra.logUi("nr-web-test","PASS "+label)}
    function near(a,b){return Math.abs(a-b)<0.0001}
    Timer {
        interval:500;running:test.media.length>0;repeat:true
        property int phase:0
        property int ticks:0
        property int total:0
        property int index:-1
        function next(p){phase=p;ticks=0}
        onTriggered:{try{
            ++ticks;if(++total>420)throw new Error("timeout phase "+phase)
            if(phase===0){
                if(ticks<4)return
                veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.setPreference("nrWebEditor",false)
                veyra.openPath(test.media);next(1);return
            }
            if(phase===1){
                if(ticks<4||!veyra.running||veyra.applying)return
                const c=card();check(c!==null,"list NR card exists");c.open=true
                index=veyra.nrLayers[0].index
                const before=JSON.stringify(veyra.nrLayers[0])
                control("nr-web-toggle").toggled(true)
                check(veyra.preferences.nrWebEditor===true,"web switch saved through the real bridge")
                check(JSON.stringify(veyra.nrLayers[0])===before,"switching the editor preserves every NR parameter")
                next(2);return
            }
            if(phase===2){
                if(ticks<3)return
                check(!control("nr-slider-editor").enabled,"original sliders are disabled")
                const change=control("nr-web-changes")
                change.selectAxis(0);change.setSelected(0.65)
                change.selectAxis(2);change.setSelected(0.8)
                control("nr-web-total").moved(1.2)
                control("nr-web-correctionEnabled").toggled(true)
                control("nr-web-correctionAuto").picked("0")
                next(3);return
            }
            if(phase===3){
                if(ticks<3||veyra.applying)return
                const p=control("nr-web-protections"),v=[0.6,0.4,0.2,0.7,0.55,0.5]
                check(p.editable,"manual protection web is editable")
                for(let i=0;i<v.length;++i){p.selectAxis(i);p.setSelected(v[i])}
                veyra.setNrLayerParameter(index,"style",1)
                control("nr-web-correctionAuto").picked("1")
                next(4);return
            }
            if(phase===4){
                if(ticks<3||veyra.applying)return
                const p=control("nr-web-protections")
                check(!p.editable&&near(p.valueAt(0),0.9)&&near(p.valueAt(1),0.25)&&near(p.valueAt(2),0.55),"auto web reads effective C++ style values")
                check(near(veyra.nrLayers[0].colorRetention,0.6),"auto preserves the saved manual values")
                control("nr-web-correctionAuto").picked("0")
                control("nr-web-tone").moved(0.85)
                control("nr-web-structure").moved(0.9)
                control("nr-web-resolution").picked("manual")
                control("nr-web-size-percent").moved(62)
                control("nr-web-layer-enabled").toggled(true)
                next(5);return
            }
            if(phase===5){
                if(ticks<5||veyra.applying)return
                const n=veyra.nrLayers[0]
                check(near(n.darken,0.65)&&near(n.color,0.8)&&near(n.total,1.2),"change web writes real NR parameters")
                check(near(n.colorRetention,0.6)&&near(n.shadowProtection,0.2)&&near(n.temporalStability,0.5),"manual protection values restored and applied")
                check(n.sizePercent===62&&n.manualSize&&near(n.tone,0.85)&&near(n.structure,0.9),"non-web sliders retain full parameter editing")
                check(n.enabled,"layer comparison switch enables NR")
                if(test.shot.length){
                    test.saved=false
                    check(card().grabToImage(result=>{test.saved=result.saveToFile(test.shot)}),"NR panel screenshot scheduled")
                }else test.saved=true
                next(6);return
            }
            if(phase===6){
                if(!test.saved||ticks<2)return
                control("nr-web-toggle").toggled(false)
                check(control("nr-slider-editor").enabled&&near(control("nr-darken").value,0.65),"return to sliders preserves web values")
                control("nr-web-toggle").toggled(true)
                veyra.logUi("nr-web-test","NR_WEB_UI_PASS");console.log("NR_WEB_UI_PASS");Qt.quit()
            }
        }catch(e){veyra.logUi("nr-web-test","NR_WEB_UI_FAIL "+e.message);console.log("NR_WEB_UI_FAIL "+e.message);Qt.quit()}}
    }
}
