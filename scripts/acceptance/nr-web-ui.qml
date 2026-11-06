import QtQuick
Item {
    id:test
    property string media:""
    property string shot:""
    property int images:0
    function find(item,name){if(item.objectName===name)return item;for(const child of item.children){const hit=find(child,name);if(hit)return hit}return null}
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
            ++ticks;if(++total>220)throw new Error("timeout phase "+phase)
            if(phase===0){
                if(ticks<3)return
                veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                check(veyra.setPreference("nrResidualView","list")&&veyra.setPreference("nrCorrectionView","list"),"real bridge accepts both group view preferences")
                check(!veyra.setPreference("nrCorrectionView","invalid"),"invalid view is rejected without replacing the selection")
                veyra.openPath(test.media);next(1);return
            }
            if(phase===1){
                if(ticks<3||!veyra.running||veyra.applying)return
                card().open=true;control("nr-residual-group").expanded=true;control("nr-correction-group").expanded=true
                index=veyra.nrLayers[0].index
                check(find(card(),"nr-web-toggle")===null&&find(card(),"nr-slider-editor")===null,"standalone editor and grey duplicate removed")
                const before=JSON.stringify(veyra.nrLayers[0])
                control("nr-residual-view").picked("web")
                check(veyra.preferences.nrResidualView==="web"&&veyra.preferences.nrCorrectionView==="list","group selections are independent")
                check(JSON.stringify(veyra.nrLayers[0])===before,"changing view preserves every NR parameter")
                next(2);return
            }
            if(phase===2){
                if(ticks<3)return
                check(control("nr-runtime").enabled&&control("nr-intensity").enabled,"runtime and model controls remain at their original active location")
                const change=control("nr-residual-web")
                check(change.visible&&change.axes.length===4&&!control("nr-darken").visible&&control("nr-total").visible,"residual web replaces only four axes; master gain stays a slider")
                change.selectAxis(0);change.setSelected(0.65);change.selectAxis(2);change.setSelected(0.8)
                control("nr-total").moved(1.2)
                control("nr-correctionEnabled").toggled(true);control("nr-correctionAuto").picked("0")
                control("nr-correction-view").picked("web");next(3);return
            }
            if(phase===3){
                if(ticks<3||veyra.applying)return
                const p=control("nr-correction-web")
                check(p.visible&&p.axes.length===9,"manual web contains all nine parameters")
                for(let i=0;i<9;++i){p.selectAxis(i);p.setSelected((i+1)/10)}
                control("nr-correction-view").picked("list");next(4);return
            }
            if(phase===4){
                if(ticks<3||veyra.applying)return
                const p=control("nr-correction-web"),n=veyra.nrLayers[0]
                for(let i=0;i<9;++i){const k=p.axes[i].key,s=control("nr-"+k);check(s.visible&&s.enabled&&near(s.value,(i+1)/10)&&near(n[k],(i+1)/10),"manual list and backend agree: "+k)}
                control("nr-correction-view").picked("web");control("nr-residual-view").picked("list")
                check(veyra.preferences.nrResidualView==="list"&&veyra.preferences.nrCorrectionView==="web","changing one group does not switch the other")
                control("nr-correctionAuto").picked("1");next(5);return
            }
            if(phase===5){
                if(ticks<3||veyra.applying)return
                check(!control("nr-correction-web").visible&&!control("nr-correction-view").visible&&control("nr-correctionAmount").visible,"automatic mode keeps only its original automatic controls")
                control("nr-correctionAmount").moved(0.7)
                control("nr-correctionAuto").picked("0");control("nr-residual-view").picked("web")
                control("nr-resolution").picked("manual");control("nr-size-percent").moved(62)
                next(6);return
            }
            if(phase===6){
                if(ticks<3||veyra.applying)return
                const p=control("nr-correction-web"),n=veyra.nrLayers[0]
                check(p.visible&&near(p.valueAt(0),0.1)&&near(p.valueAt(1),0.2)&&near(p.valueAt(2),0.3)&&near(n.correctionAmount,0.7),"manual values and view survive automatic mode")
                check(near(n.darken,0.65)&&near(n.color,0.8)&&near(n.total,1.2)&&n.sizePercent===62,"residual and outside controls share the original settings")
                if(test.shot.length){
                    test.images=0
                    check(card().grabToImage(r=>{if(r.saveToFile(test.shot))++test.images}),"full panel screenshot scheduled")
                    check(control("nr-correction-group").grabToImage(r=>{if(r.saveToFile(test.shot.replace(".png","-manual.png")))++test.images}),"nine-parameter manual panel screenshot scheduled")
                }else test.images=2
                next(7);return
            }
            if(phase===7){
                if(test.images<2||ticks<2)return
                veyra.logUi("nr-web-test","NR_WEB_UI_PASS");console.log("NR_WEB_UI_PASS");Qt.quit()
            }
        }catch(e){veyra.logUi("nr-web-test","NR_WEB_UI_FAIL "+e.message);console.log("NR_WEB_UI_FAIL "+e.message);Qt.quit()}}
    }
}
