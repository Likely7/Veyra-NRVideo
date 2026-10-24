// Loaded only into the task's private staging copy by nr-manual-scale.py.
import QtQuick
Item {
    id: test
    property string config: "{}"
    readonly property var cfg: JSON.parse(config)
    property int phase: 0
    property int ticks: 0
    property int total: 0
    property real beforePosition: 0
    property var percents: [50,25,37,75,100]
    property int choice: 0
    function check(ok, why) { if (!ok) throw new Error(why) }
    function next(n) { phase=n; ticks=0 }
    function edit(percent) { check(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePercent",percent),"manual edit "+percent) }
    function out(name) { console.log("NR_MANUAL",name,JSON.stringify({phase:phase,position:veyra.position,layers:veyra.nrLayers,nodeMode:veyra.nodeMode,nrActive:veyra.nrActive,summary:veyra.sourceSummary})) }
    function find(item,name) {
        if (!item) return null
        if (item.objectName===name) return item
        for (const child of item.children || []) { const found=find(child,name);if(found)return found }
        return null
    }
    Timer {
        id: timer; interval: 250; running: test.config!=="{}"; repeat: true
        onTriggered: { try {
            ++test.ticks; ++test.total
            if (test.total>680) throw new Error("timeout phase "+test.phase)
            if(test.phase===0) {
                if(test.ticks<8)return
                root.page="pro";veyra.muted=true;veyra.srEnabled=false;veyra.fgEnabled=false
                if(test.cfg.restore) {
                    check(veyra.nodeMode===0&&veyra.nrLayers[0].manualSize&&veyra.nrLayers[0].sizePercent===50,"list percentage restored on startup")
                    veyra.nodeMode=1
                    check(veyra.nrLayers[0].manualSize&&veyra.nrLayers[0].sizePercent===75,"node percentage restored on startup")
                    veyra.nodeMode=0;out("RESTORE_PASS");timer.stop();Qt.quit();return
                }
                veyra.nrEnabled=false;veyra.nodeMode=0
                const index=veyra.nrLayers[0].index
                check(veyra.setNrLayerParameter(index,"runtime",2),"SF runtime")
                check(!veyra.setNrLayerParameter(index,"sizePercent",24)&&!veyra.setNrLayerParameter(index,"sizePercent",101)&&!veyra.setNrLayerParameter(index,"sizePercent",50.5)&&!veyra.setNrLayerParameter(index,"sizePercent",NaN),"invalid percentage rejected")
                if(test.cfg.captureOnly){edit(37);veyra.nrEnabled=true;veyra.openPath(test.cfg.media);next(10);return}
                edit(50);veyra.nrEnabled=true;veyra.openPath(test.cfg.media);next(1);return
            }
            if(test.phase===1) {
                if(test.ticks<18||veyra.applying||!veyra.running||veyra.position<=test.beforePosition+.5)return
                check(veyra.nrActive&&!veyra.failed&&veyra.position>test.beforePosition+.5,"real NR playback "+test.percents[test.choice]+" "+veyra.statusText)
                check(veyra.nrLayers[0].manualSize&&veyra.nrLayers[0].sizePercent===test.percents[test.choice],"bridge snapshot percentage")
                out("GPU_PERCENT_"+test.percents[test.choice])
                if(++test.choice<test.percents.length){test.beforePosition=veyra.position;edit(test.percents[test.choice]);test.ticks=0;return}
                edit(50);next(2);return
            }
            if(test.phase===2) {
                if(test.ticks<16||veyra.applying)return
                check(veyra.savePresetAs("NR manual 50",15,false),"save real list preset")
                veyra.nodeMode=1;root.page="node";next(3);return
            }
            if(test.phase===3) {
                if(test.ticks<16||veyra.applying)return
                if(veyra.nrLayers.length===0) {
                    const index=veyra.addEffect("nr")
                    check(index>=0,"add independent NR node")
                    const row=veyra.chain.find(n=>n.index===index)
                    check(row&&veyra.setEffectEnabled(index,true)&&veyra.insertNodeAfter(row.id,0),"connect NR after input")
                }
                check(veyra.chainValid,"node graph valid")
                edit(75);next(4);return
            }
            if(test.phase===4) {
                if(test.ticks<16||veyra.applying)return
                check(veyra.nrActive&&veyra.nrLayers[0].sizePercent===75,"node GPU manual input")
                out("NODE_75_PASS")
                veyra.nodeMode=0;root.page="pro";next(5);return
            }
            if(test.phase===5) {
                if(test.ticks<16||veyra.applying)return
                check(veyra.nrLayers[0].sizePercent===50,"independent list state")
                const stage=find(root.contentItem,"list-nr-stage"),card=find(root.contentItem,"nr-card-1")
                if(stage)stage.open=true
                if(card)card.open=true
                next(6);return
            }
            if(test.phase===6) {
                if(test.ticks<8)return
                const card=find(root.contentItem,"nr-card-1")
                const slider=find(card,"nr-size-percent")
                check(slider&&slider.visible&&slider.value===50,"real list manual slider visible")
                card.grabToImage(result=>result.saveToFile(test.cfg.screenshot))
                veyra.nrEnabled=false;next(7);return
            }
            if(test.phase===7) {
                if(test.ticks<8||veyra.applying)return
                out("GPU_AND_UI_PASS");timer.stop();Qt.quit()
            }
            if(test.phase===10) {
                if(test.ticks<16||veyra.applying||!veyra.nrActive||veyra.position<1)return
                check(veyra.screenshotDirectory===test.cfg.screenshotDir,"private screenshot path")
                veyra.takeScreenshot();next(11);return
            }
            if(test.phase===11) {
                if(test.ticks<12)return
                out("PIXELS_PASS");timer.stop();Qt.quit()
            }
        } catch(e) { console.error("NR_MANUAL_FAIL",test.phase,e.message);timer.stop();Qt.quit() } }
    }
}
