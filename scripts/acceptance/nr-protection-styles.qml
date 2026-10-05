// Same source frame, real Feature-18 provider, three styles at residual gain 5.
import QtQuick
Item {
    id: test
    property string media: ""
    property string evidence: ""
    property var scenarios: []
    property int step: 0
    property int ticks: 0
    property int entered: 0
    property int current: -1
    property bool fixedImage: false
    function require(ok,why){if(!ok){console.error("NR_STYLES_FAIL",why);timer.stop();Qt.quit();throw new Error(why)}}
    function edit(key,value){require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,key,value),"edit "+key)}
    function next(s){step=s;entered=ticks}
    Timer {
        id: timer;interval: 100;repeat: true;running: true
        onTriggered: {
            if(++test.ticks>2300){test.require(false,"230s deadline");return}
            if(!test.media.length||test.ticks<12)return
            if(test.step===0){
                veyra.nodeMode=0;veyra.nrEnabled=true;veyra.srEnabled=false;veyra.fgEnabled=false
                test.edit("runtime",3);test.edit("style",0);test.edit("total",5)
                test.require(veyra.setPreference("screenshotDir",test.evidence+"/screenshots"),"screenshot path")
                veyra.openPath(test.media);test.next(1)
            }else if(test.step===1){
                test.require(!veyra.failed,"preview failed: "+veyra.statusText)
                if(!veyra.nrActive||(!test.fixedImage&&veyra.position<.8))return
                if(!test.fixedImage&&!veyra.paused)veyra.togglePlayPause()
                veyra.nrEnabled=false;test.next(2)
            }else if(test.step===2){
                if(test.ticks-test.entered<20)return
                console.log("NR_STYLES_CAPTURE","source",veyra.position,JSON.stringify(veyra.nrLayers))
                veyra.takeScreenshot();test.next(3)
            }else if(test.step===3){
                if(test.ticks-test.entered<12)return
                ++test.current
                if(test.current>=test.scenarios.length){
                    console.log("NR_STYLES_PASS");timer.stop();Qt.quit();return
                }
                const s=test.scenarios[test.current]
                veyra.nrEnabled=true
                test.edit("style",s.style);test.edit("correctionEnabled",s.enabled?1:0)
                test.edit("correctionAuto",s.automatic?1:0)
                for(const key of Object.keys(s.values||{}))test.edit(key,s.values[key])
                test.next(4)
            }else if(test.step===4){
                if(test.ticks-test.entered<25)return
                test.require((test.fixedImage||veyra.paused)&&veyra.nrActive&&!veyra.failed,"inactive controlled preview")
                const s=test.scenarios[test.current],n=veyra.nrLayers[0]
                test.require(n.style===s.style&&n.runtime===3&&n.total===5,"wrong provider/style/gain")
                console.log("NR_STYLES_CAPTURE",s.label,veyra.position,JSON.stringify(n),JSON.stringify(veyra.stageTimings))
                veyra.logUi("nr-styles",s.label+" "+JSON.stringify(n))
                veyra.takeScreenshot();test.next(3)
            }
        }
    }
}
