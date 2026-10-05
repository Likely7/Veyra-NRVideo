import QtQuick

Item {
    id: test
    property string media: ""
    property bool restore: false
    property int phase: 0
    property int ticks: 0
    property double startPosition: 0
    property var expected: ({})
    function require(ok,why) { if (!ok) throw new Error(why) }
    function values() {
        return {backend:veyra.fgBackendName,multiplier:veyra.fgMultiplier,
            vfgQuality:veyra.vfgQuality,motion:veyra.fgMotionSource,
            opticalFlow:veyra.opticalFlowChoice,amdHalf:veyra.amdFlowHalf,
            flowQuality:veyra.flowQuality,contentRate:veyra.contentRate,
            strict:veyra.fgStrict,lowQueue:veyra.fgLowQueue}
    }
    function check(want) {
        const got=values()
        for (const key in want) require(got[key]===want[key],key+" expected="+want[key]+" got="+got[key])
    }
    Timer {
        interval:250;running:true;repeat:true
        onTriggered: { try {
            ++test.ticks
            test.require(test.ticks<400,"100s UI deadline phase="+test.phase)
            test.require(!veyra.failed,"playback failed "+veyra.statusText)
            if (test.phase===0) {
                if(test.ticks<8)return
                test.require(veyra.setPreference("listInspectorWidth",480),"panel width preference")
                test.require(Number(veyra.preferences.listInspectorWidth)===480,"panel width not applied")
                test.require(!veyra.setPreference("listInspectorWidth",1),"invalid panel width accepted")
                test.require(veyra.preferences.gpuPriority===undefined||veyra.preferences.gpuPriority==="realtime","fresh profile default")
                test.require(veyra.gpuPriorityStatus.length>0,"GPU priority status missing")
                veyra.nodeMode=0;veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                if(test.restore) {
                    const rows=veyra.fgPresets
                    test.require(rows.length===2&&rows[0].name==="204 persisted FG","FG preset did not survive restart")
                    test.require(veyra.applyFgPreset(rows[0].index),"restore apply failed")
                    test.check({backend:"dlss",multiplier:2,opticalFlow:0,amdHalf:true,flowQuality:0,contentRate:3,strict:true,lowQueue:true})
                    test.require(!veyra.applyFgPreset(rows[1].index),"rejected field incorrectly reported as a complete preset")
                    test.require(veyra.flowQuality===0,"rejected field replaced the last valid setting")
                    console.log("RELEASE_204_FG_REJECT_PASS")
                    test.require(veyra.deleteFgPreset(1)&&veyra.deleteFgPreset(0)&&veyra.fgPresets.length===0,"delete persisted preset")
                    console.log("RELEASE_204_UI_RESTORE_PASS",JSON.stringify(test.values()));Qt.quit();return
                }
                veyra.fgBackendName="dlss";veyra.fgMultiplier=2;veyra.vfgQuality=2
                veyra.fgMotionSource=1;veyra.setOpticalFlowChoice(0);veyra.amdFlowHalf=true;veyra.flowQuality=0;veyra.contentRate=3
                veyra.fgStrict=true;veyra.fgLowQueue=true
                test.expected=test.values()
                test.require(veyra.saveFgPreset("204 persisted FG",false),"FG save")
                test.require(!veyra.saveFgPreset("204 persisted FG",false),"duplicate save silently accepted")
                test.require(veyra.saveFgPreset("204 temporary",false),"second FG save")
                test.require(veyra.fgPresets.length===2,"saved FG count")
                veyra.fgEnabled=false;veyra.flowQuality=2;veyra.contentRate=0;veyra.amdFlowHalf=false;veyra.fgStrict=false;veyra.fgLowQueue=false
                test.require(veyra.applyFgPreset(0),"FG apply")
                test.check(test.expected)
                test.require(veyra.deleteFgPreset(1)&&veyra.fgPresets.length===1,"FG delete")
                console.log("RELEASE_204_FG_PRESET_PASS",JSON.stringify(test.values()))
                veyra.fgEnabled=false;veyra.openPath(test.media);test.phase=1
            } else if(test.phase===1) {
                if(!veyra.running||veyra.applying||veyra.position<.5)return
                test.startPosition=veyra.position
                veyra.setPreference("listInspectorWidth",540)
                veyra.page="min";veyra.page="pro"
                test.phase=2
            } else if(test.phase===2) {
                if(veyra.position<test.startPosition+1)return
                test.require(veyra.running,"width/page change stopped playback")
                test.require(Number(veyra.preferences.listInspectorWidth)===540,"panel width changed unexpectedly")
                test.require(veyra.gpuPriorityStatus.length>0,"GPU priority status missing during playback")
                console.log("RELEASE_204_UI_PASS",veyra.position,veyra.gpuPriorityStatus)
                veyra.stopPlayback();Qt.quit()
            }
        } catch(e) { console.error("RELEASE_204_UI_FAIL",e.message);Qt.quit() } }
    }
}
