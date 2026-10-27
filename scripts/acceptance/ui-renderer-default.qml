// Loaded only into a private staging copy, never the user's running package.
// claude-software-ui-default-20261026: one launch of a four-launch sequence on
// one private profile. Opens 设置 → 兼容, logs the actual renderer and the
// 界面绘制 choice, picks a value through the real control and records whether
// the restart confirmation opened.
import QtQuick
Item {
    id: test
    property int step: 0
    property string shot: ""
    property int phase: 0
    property int ticks: 0
    property bool shotSaved: false
    function check(ok,why){if(!ok)throw new Error(why)}
    function find(item,name){
        if(!item)return null
        if(item.objectName===name)return item
        const kids=item.children||[]
        for(let i=0;i<kids.length;++i){const found=find(kids[i],name);if(found)return found}
        return null
    }
    function log(text){veyra.logUi("ui-renderer-test",text);console.log("UIR",text)}
    Timer {interval: 250;repeat: true;running: test.step>0
        onTriggered: {try {
            ++test.ticks
            if(test.phase===0){
                if(test.ticks<6)return
                root.page="set";settingsPage.section="compat";test.phase=1;test.ticks=0
            }else if(test.phase===1){
                if(test.ticks<8)return
                const seg=find(settingsPage,"set-ui-renderer"),confirm=find(settingsPage,"obs-restart-confirm")
                check(seg!==null&&confirm!==null,"界面绘制 control and restart confirmation found")
                const expectedSr=test.step===1?4:test.step===4?5:0
                check(veyra.videoSrQuality===expectedSr,"fresh RTX Video SR or saved SR selection restored")
                check(!veyra.srEnabled,"SR stays off while its model choice changes")
                log("sr-selection="+veyra.videoSrQuality+" enabled="+veyra.srEnabled)
                log("step="+test.step+" softwareUiActive="+veyra.softwareUiActive+" overlayCompatActive="+veyra.overlayCompatActive
                    +" pref="+(veyra.preferences.uiRenderer||"")+" segment="+seg.current)
                // The grab is asynchronous: pick only after the PNG of the state before the pick is written.
                if(test.shot!=="")root.contentItem.grabToImage(r=>{test.shotSaved=r.saveToFile(test.shot)})
                test.phase=2;test.ticks=0
            }else if(test.phase===2){
                if(test.shot!==""&&!test.shotSaved&&test.ticks<20)return
                check(test.shot===""||test.shotSaved,"screenshot saved")
                const seg=find(settingsPage,"set-ui-renderer"),confirm=find(settingsPage,"obs-restart-confirm")
                // 1 picks GPU from the software default, 2 picks 软件 from GPU (then GPU is
                // stored again for 3), 3 picks 软件 while compatibility keeps it in software.
                const target=test.step===1?"gpu":test.step===2||test.step===3?"software":""
                if(target!==""){
                    seg.picked(target)
                    log("picked="+target+" pref="+(veyra.preferences.uiRenderer||"")+" confirmShown="+confirm.shown)
                    if(confirm.shown){confirm.close();confirm.rejected()}
                }
                if(test.step===2)check(veyra.setPreference("uiRenderer","gpu"),"GPU stored again for the next launch")
                if(test.step===1){veyra.videoSrQuality=0;check(veyra.videoSrQuality===0,"explicit DLSS selection accepted")}
                if(test.step===3){veyra.videoSrQuality=5;check(veyra.videoSrQuality===5,"explicit FSR selection accepted")}
                test.phase=3;test.ticks=0
            }else if(test.phase===3){
                if(test.ticks<4)return
                log("done pref="+(veyra.preferences.uiRenderer||""));console.log("UIR_PASS");Qt.quit()
            }
        }catch(e){console.error("UIR_FAIL",test.phase,e.message);Qt.quit()}}
    }
}
