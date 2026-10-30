import QtQuick
Item {
    id: test
    property bool verifyOnly:false
    property int step:0
    property int ticks:0
    function check(ok,message){if(!ok)throw Error(message);veyra.logUi("rdna3-ui-test","PASS "+message)}
    function currentNrLayer(){const rows=veyra.nrLayers;check(rows.length>0,"NR layer exists");return rows[0]}
    function finish(){console.log("RDNA3_UI_PASS");Qt.quit()}
    Timer {
        interval:200;repeat:true;running:true
        onTriggered:{try{
            ++test.ticks
            if(test.ticks>100)throw Error("UI fixture timeout")
            if(test.step===0){
                const amd=veyra.nrRuntimeChoices.find(x=>String(x.id)==="4")
                check(amd!==undefined&&amd.disabled,"AMD runtime files never make RX7000 NR available on this NVIDIA adapter")
                veyra.nrEnabled=false
                if(test.verifyOnly){
                    veyra.nodeMode=1;veyra.navigate("node");test.step=9;return
                }
                const nr=currentNrLayer()
                check(veyra.setNrLayerParameter(nr.index,"performance",0),"save Original model choice through the real bridge")
                check(veyra.setNrLayerParameter(nr.index,"sizePercent",62),"save manual processing percentage independently")
                test.step=1;return
            }
            if(test.step===1){
                const nr=currentNrLayer();check(!nr.performance&&nr.sizePercent===62,"list model and percentage reflect saved values")
                veyra.nodeMode=1;if(!veyra.nrLayers.length)check(veyra.addEffect("nr")>=0,"create NR in an empty node graph");veyra.navigate("node");test.step=2;return
            }
            if(test.step===2){
                const nr=currentNrLayer()
                check(veyra.setNrLayerParameter(nr.index,"performance",0),"node saves its independent Original choice")
                check(veyra.setNrLayerParameter(nr.index,"sizePercent",37),"node saves its independent 37 percent")
                test.step=3;return
            }
            if(test.step===3){const nr=currentNrLayer();check(!nr.performance&&nr.sizePercent===37,"node retains both choices");test.step=4;return}
            if(test.step===4&&test.ticks>8){finish();return}
            if(test.step===9){const nr=currentNrLayer();check(!nr.performance&&nr.sizePercent===37,"node model and percent survive a full application restart");finish()}
        }catch(e){veyra.logUi("rdna3-ui-test","RDNA3_UI_FAIL "+e.message);console.log("RDNA3_UI_FAIL "+e.message);Qt.quit()}}
    }
}
