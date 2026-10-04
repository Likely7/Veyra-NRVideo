import QtQuick
import QtQuick.Window
Item {
    id: test
    property var win: Window.window
    property string media: ""
    property string mode: "layout"
    property int phase: 0
    property int ticks: 0
    property double start: Date.now()
    property var widths: [1280, 720, 1600]
    property int index: 0
    property var bar: null
    property var menu: null
    property size before: Qt.size(0,0)
    function require(ok, why) {
        if (ok) return
        console.error("PLAYBACK_LAYOUT_FAIL", phase, why)
        timer.stop(); Qt.quit(); throw new Error(why)
    }
    function find(item,name,depth) {
        if(!item)return null
        depth=depth||0
        if(depth>20)return null
        if(item.objectName===name)return item
        const children=item.data||item.contentData||item.children||[]
        for(let i=0;i<children.length;++i){const f=find(children[i],name,depth+1);if(f)return f}
        if(item.contentItem)return find(item.contentItem,name,depth+1)
        return null
    }
    function rect(item) {
        const p=item.mapToItem(null,0,0)
        return Qt.rect(p.x,p.y,item.width,item.height)
    }
    Timer {
        id: timer
        interval:200;running:true;repeat:true
        onTriggered: {
            ++test.ticks
            test.require(test.ticks<350,"70-second deadline")
            if(!test.media.length)return
            if(test.phase===0){
                veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.openPath(test.media);test.phase=1;return
            }
            if(test.phase===1){
                if(!veyra.running||veyra.applying)return
                test.win.width=test.widths[test.index];test.win.height=800
                test.start=Date.now();test.phase=2;return
            }
            if(test.phase===2){
                if(Date.now()-test.start<1200)return
                const header=test.find(test.win,"pro-header")
                const format=test.find(test.win,"pro-format-tag")
                const output=test.find(test.win,"pro-output-summary")
                const actions=test.find(test.win,"pro-header-actions")
                const seek=test.find(test.win,"pro-seek")
                test.require(!!header&&!!format&&!!output&&!!actions&&!!seek,"professional controls missing")
                const a=test.rect(format), b=test.rect(output), h=test.rect(header), c=test.rect(actions)
                test.require(output.visible&&a.y===b.y&&b.x>=a.x+a.width&&b.x-a.x-a.width<=12,"output is not beside input")
                test.require(b.x+b.width<=h.x+h.width+1&&c.x>=h.x-1&&c.x+c.width<=h.x+h.width+1,"header controls outside viewport")
                test.require(!header.compact||c.y>=b.y+b.height,"narrow header rows overlap")
                test.require(test.widths[test.index]!==1280||seek.width>=200,"seek bar remains crowded")
                test.require(seek.width>=100,"narrow window has no usable seek bar")
                console.log("PLAYBACK_PRO_LAYOUT_PASS",test.win.width,seek.width,header.height,JSON.stringify(b))
                ++test.index
                if(test.index<test.widths.length){test.phase=1;return}
                test.win.width=1280;test.win.height=800;test.before=Qt.size(1280,800)
                test.win.toggleFullscreen();test.start=Date.now();test.phase=3;return
            }
            if(test.phase===3){
                if(Date.now()-test.start<2600)return
                test.require(test.win.fullscreen,"fullscreen did not enter")
                test.require(Math.abs(test.win.width-test.win.screen.width)<=2&&Math.abs(test.win.height-test.win.screen.height)<=2,"fullscreen does not cover monitor")
                test.bar=test.find(test.win,"fullscreenBar")
                test.require(!!test.bar,"fullscreen bar missing")
                if(test.bar.hovered)console.log("PLAYBACK_FULLSCREEN_AUTOHIDE_SKIPPED","physical pointer is on controls")
                else test.require(!test.win.fullControls&&!test.bar.visible,"idle fullscreen controls remain visible")
                test.win.pointerActivity()
                const button=test.find(test.bar,"cine-playback-rate")
                test.menu=test.find(button,"cine-playback-rate-menu")
                test.require(!!test.menu,"fullscreen rate menu missing")
                test.menu.openAt(button,"up");test.start=Date.now();test.phase=4;return
            }
            if(test.phase===4){
                if(Date.now()-test.start<2400)return
                test.require(test.bar.menuOpen&&test.win.fullControls&&test.bar.visible,"open menu was hidden with fullscreen controls")
                test.require(test.bar.hitRect.height===test.bar.height,"fullscreen menu is clipped")
                test.menu.close();test.win.toggleLock();test.win.pointerActivity()
                test.require(test.win.fullLocked&&!test.bar.shown,"locked controls responded to pointer activity")
                test.win.toggleFullscreen();test.start=Date.now();test.phase=5;return
            }
            if(test.phase===5){
                if(Date.now()-test.start<1200)return
                test.require(!test.win.fullscreen&&!test.win.fullLocked,"fullscreen exit retained lock")
                test.require(Math.abs(test.win.width-test.before.width)<=2&&Math.abs(test.win.height-test.before.height)<=2,"windowed geometry did not restore")
                console.log("PLAYBACK_FULLSCREEN_WINDOWED_PASS",test.win.width,test.win.height)
                test.win.toggleMaximized();test.start=Date.now();test.phase=6;return
            }
            if(test.phase===6){
                if(Date.now()-test.start<1200)return
                test.require(test.win.maximized,"maximize failed")
                test.win.toggleFullscreen();test.start=Date.now();test.phase=7;return
            }
            if(test.phase===7){
                if(Date.now()-test.start<1200)return
                test.require(test.win.fullscreen,"maximized fullscreen did not enter")
                test.win.toggleFullscreen();test.start=Date.now();test.phase=8;return
            }
            if(test.phase===8){
                if(Date.now()-test.start<1200)return
                test.require(test.win.maximized,"fullscreen exit lost the previous maximized state")
                console.log("PLAYBACK_FULLSCREEN_MAXIMIZED_PASS")
                console.log("PLAYBACK_LAYOUT_FULLSCREEN_PASS")
                Qt.quit()
            }
        }
    }
}
