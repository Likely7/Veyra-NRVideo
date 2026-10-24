import QtQuick
Item {
    id: test
    property string config: "{}"
    readonly property var cfg: JSON.parse(config)
    property int phase: 0
    property int ticks: 0
    property int total: 0
    function check(ok,why){if(!ok)throw new Error(why)}
    function next(n){phase=n;ticks=0}
    function sample(mark){console.log("CAPTURE_FG_"+mark,JSON.stringify({caseName:cfg.name,half:cfg.half,format:veyra.captureFormatLabel,source:veyra.sourceSummary,fgActive:veyra.fgActive,backend:veyra.fgBackendName,submitFps:veyra.submitFps,inputFps:veyra.captureFps,dropped:veyra.captureDropped,status:veyra.runStatusDetail}))}
    Timer {id: timer;interval:250;repeat:true;running:test.config!=="{}"
        onTriggered:{try{
            ++test.ticks;if(++test.total>480)throw new Error("120 second deadline phase "+test.phase)
            if(test.phase===0){
                if(test.ticks<8)return
                root.page="pro";veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.refreshCaptureDevices();next(1)
            }else if(test.phase===1){
                if(veyra.captureQueryBusy||test.ticks<4)return
                const device=veyra.captureDevices.find(x=>x.label.indexOf("KUHAIMI G50")>=0)
                check(!!device,"KUHAIMI G50 not present")
                veyra.captureDeviceId=device.id;next(2)
            }else if(test.phase===2){
                if(veyra.captureQueryBusy||test.ticks<4)return
                const format=veyra.captureFormats.find(x=>x.width===2560&&x.height===1440&&Math.abs(x.fps-60)<0.01&&x.pixel==="NV12"&&!x.interlaced&&!x.disabled)
                check(!!format,"exact 2560x1440p60 NV12 not advertised")
                veyra.captureFormatKey=format.id;veyra.captureAudioChoice=-1;veyra.captureBufferMode=0
                veyra.contentRate=test.cfg.half?5:0;veyra.outputRateMode=0;veyra.variableRefresh=false;veyra.displaySync=2
                check(veyra.addEffect("vfg-fg")>=0,"VFG stage")
                veyra.vfgQuality=1;veyra.fgMultiplier=2
                check(veyra.startCaptureSession(),"capture open refused: "+veyra.captureStatus)
                next(3)
            }else if(test.phase===3){
                if(!veyra.running||!veyra.fgActive||veyra.applying||test.ticks<24)return
                check(veyra.captureFormatLabel.indexOf("NV12")>=0,"selection changed pixel format")
                sample("BEGIN");next(4)
            }else if(test.phase===4){
                check(veyra.running&&veyra.fgActive,"capture or VFG stopped: "+veyra.statusText)
                if(test.ticks%4===0)sample("SAMPLE")
                if(test.ticks<test.cfg.seconds*4)return
                sample("END");veyra.takeScreenshot();next(5)
            }else if(test.phase===5){
                if(test.ticks<8)return
                veyra.stopPlayback();next(6)
            }else if(test.phase===6){
                if(test.ticks<8)return
                check(!veyra.running,"capture did not stop")
                console.log("CAPTURE_FG_PASS",test.cfg.name);timer.stop();Qt.quit()
            }
        }catch(e){console.error("CAPTURE_FG_FAIL",test.cfg.name,test.phase,e.message);veyra.stopPlayback();timer.stop();Qt.quit()}}
    }
}
