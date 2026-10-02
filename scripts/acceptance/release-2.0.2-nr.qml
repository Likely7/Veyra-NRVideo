    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 250; running: true; repeat: true
        property int phase: 0
        property int ticks: 0
        property int total: 0
        property int choice: 0
        property real beforePosition: 0
        property var choices: [0, 2, 3, 0]
        property bool restore: Qt.application.arguments.indexOf("--nr-restore") >= 0
        function check(ok, message) { if (!ok) throw new Error(message) }
        function next(p) { phase=p; ticks=0 }
        function preset(name) { for (var i=0;i<veyra.presets.length;++i) if(veyra.presets[i].name===name)return i;return -1 }
        function choose(id) {
            beforePosition=veyra.position
            check(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",id),"select NR "+id)
            check(veyra.nrLayers.every(n=>n.runtime===id),"all layers select NR "+id)
        }
        function findItem(item, name) {
            if (item.objectName===name) return item
            const children=item.children || []
            for(var i=0;i<children.length;++i){const found=findItem(children[i],name);if(found)return found}
            return null
        }
        onTriggered: { try {
            ++ticks; ++total
            if(total>980||ticks>180)throw new Error("bounded wait phase="+phase)
            if(phase===0){
                if(ticks<8)return
                if(restore){
                    check(veyra.nrLayers.every(n=>n.runtime===3),"list original restores after restart")
                    veyra.nodeMode=1
                    check(veyra.nrLayers.every(n=>n.runtime===3),"node original restores after restart")
                    check(veyra.applyPresetIndex(preset("2.0.2 original node")),"original node preset loads")
                    check(veyra.nrLayers.every(n=>n.runtime===3),"original node preset retains variant")
                    veyra.nodeMode=0
                    check(veyra.applyPresetIndex(preset("2.0.2 original list")),"original list preset loads")
                    check(veyra.nrLayers.every(n=>n.runtime===3),"original list preset retains variant")
                    veyra.logUi("uitest","UITEST_DONE original NR persistence");running=false;root.close();return
                }
                check(veyra.nrLayers[0].runtime===0,"RTX 50 fresh Lecram default unchanged")
                root.goPage("pro");veyra.muted=true;veyra.srEnabled=false;veyra.fgEnabled=false;veyra.nrEnabled=true
                veyra.openUrl("@VIDEO@");next(1);return
            }
            if(phase===1){
                if(ticks<16||!veyra.running||veyra.applying)return
                check(veyra.nrActive,"initial NR evaluates")
                const selector=findItem(root.contentItem,"nr-runtime")
                check(selector&&selector.options.length===3,"three options exposed in NR editor")
                check(selector.options.map(x=>x.id).join(",")==="0,2,3","persistent NR IDs exposed")
                check(!veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",1),"retired ID rejected by UI")
                check(!veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",4),"invalid ID rejected")
                check(veyra.duplicateNrLayer(veyra.nrLayers[0].index)>=0,"add second NR layer")
                choice=0;choose(choices[choice]);next(2);return
            }
            if(phase===2||phase===4){
                if(ticks<16||veyra.applying)return
                check(veyra.running&&veyra.nrActive&&veyra.position>beforePosition+0.5,"NR playback advances "+choices[choice])
                check(veyra.nrLayers.every(n=>n.runtime===choices[choice]),"active variant retained")
                veyra.logUi("uitest","NR_VARIANT_PASS mode="+veyra.nodeMode+" id="+choices[choice]+" position="+veyra.position)
                if(++choice<choices.length){choose(choices[choice]);ticks=0;return}
                choose(3)
                if(phase===2){
                    check(veyra.savePresetAs("2.0.2 original list",15,false),"save original list preset")
                    veyra.nodeMode=1;root.goPage("node");next(3);return
                }
                check(veyra.savePresetAs("2.0.2 original node",15,true),"save original node preset")
                veyra.nodeMode=0;root.goPage("pro");next(5);return
            }
            if(phase===3){
                if(ticks<12||veyra.applying)return
                if(!veyra.nrLayers.length)check(veyra.addEffect("nr")>=0,"add node NR")
                choice=0;choose(choices[choice]);next(4);return
            }
            if(phase===5){
                if(ticks<16||veyra.applying)return
                check(veyra.nrActive&&veyra.nrLayers.every(n=>n.runtime===3),"list original resumes after node mode")
                veyra.logUi("uitest","UITEST_SHOT original-nr")
                veyra.logUi("uitest","UITEST_DONE all NR variants");running=false;root.close()
            }
        } catch(e) {veyra.logUi("uitest","UITEST_FAIL "+e.message);running=false;root.close()} }
    }
