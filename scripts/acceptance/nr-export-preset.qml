import QtQuick
Item {
    id: test
    property string media: ""
    property string output: ""
    property int phase: 0
    property int ticks: 0
    property int total: 0
    function check(ok,why){if(!ok)throw new Error(why)}
    function next(n){phase=n;ticks=0}
    Timer {id:timer;interval:250;repeat:true;running:test.media!==""
        onTriggered:{try{
            ++test.ticks;if(++test.total>520)throw new Error("130 second timeout")
            if(test.phase===0){
                if(test.ticks<8)return
                root.page="pro";veyra.muted=true;veyra.nodeMode=0;veyra.srEnabled=false;veyra.fgEnabled=false;veyra.nrEnabled=true
                const index=veyra.nrLayers[0].index
                check(veyra.setNrLayerParameter(index,"runtime",2),"SF runtime")
                check(veyra.setNrLayerParameter(index,"sizePercent",37),"manual 37%")
                check(veyra.savePresetAs("NR export percent 37",15,false),"save manual preset")
                const preset=veyra.presets.findIndex(x=>x.name==="NR export percent 37");check(preset>=0,"saved preset listed")
                check(veyra.selectExportPreset(preset),"choose independent export preset")
                check(veyra.setNrLayerParameter(index,"sizePercent",100),"different current slider")
                veyra.nrEnabled=false
                veyra.exportCompletionSound=false;veyra.exportHevc=true;veyra.exportSrTargetIndex=-1;veyra.exportRateControl=1;veyra.exportBitrateMbps=24
                veyra.enqueueExportFile(test.media,test.output);next(1)
            }else if(test.phase===1){
                if(veyra.exportReadyCount!==1)return
                veyra.startExport();check(veyra.exportRunning,"worker did not start")
                check(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePercent",25),"edit current preview after freeze")
                next(2)
            }else if(test.phase===2){
                if(veyra.exportRunning)return
                check(veyra.exportItems.filter(x=>x.state==="done").length===1,"export failed: "+veyra.exportStatus)
                check(veyra.nrLayers[0].sizePercent===25&&!veyra.nrEnabled,"export changed current preview settings")
                console.log("NR_EXPORT_PRESET_PASS",veyra.exportPresetName,veyra.exportStatus);timer.stop();Qt.quit()
            }
        }catch(e){console.error("NR_EXPORT_PRESET_FAIL",test.phase,e.message);veyra.cancelExport();timer.stop();Qt.quit()}}
    }
}
