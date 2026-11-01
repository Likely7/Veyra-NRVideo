import QtQuick
import QtQuick.Window
Item {
    id: test
    property var shell
    property int phase: 0
    property int ticks: 0
    property int total: 0
    property int maxWidth: 0
    property int maxHeight: 0
    property string screenshotNotice: ""
    function fail(reason) { console.error("REVIEW_UI_FAIL",reason); timer.stop(); Qt.quit() }
    function next(p) { phase=p; ticks=0 }
    Connections {
        target: test.shell || null
        ignoreUnknownSignals: true
        function onScreenSizedChanged() {
            if(test.phase>=2&&test.phase<=4&&!test.shell.screenSized)
                test.fail("cinema resize was enabled during maximized/fullscreen restoration")
        }
    }
    Connections {
        target: veyra
        function onNotice(text,error) {
            if(text.indexOf("截图保存到：")===0)test.screenshotNotice=text
        }
    }
    Timer {
        id: timer
        interval: 250; repeat: true; running: test.shell!==undefined&&test.shell!==null
        onTriggered: {
            ++test.ticks
            if(++test.total>120){test.fail("timeout");return}
            if(test.phase===0){
                if(!veyra.running||veyra.applying||test.ticks<6)return
                if(!veyra.paused)veyra.togglePlayPause()
                test.shell.goPage("min");test.next(1)
            } else if(test.phase===1&&test.ticks>=3){
                test.shell.visibility=Window.Maximized;test.next(2)
            } else if(test.phase===2&&test.ticks>=3){
                if(test.shell.visibility!==Window.Maximized){test.fail("not maximized");return}
                test.maxWidth=test.shell.width;test.maxHeight=test.shell.height
                test.next(3);test.shell.toggleFullscreen()
            } else if(test.phase===3&&test.ticks>=3){
                if(!test.shell.fullscreen){test.fail("not fullscreen");return}
                test.next(4);test.shell.toggleFullscreen()
            } else if(test.phase===4&&test.ticks>=6){
                if(test.shell.visibility!==Window.Maximized||test.shell.width!==test.maxWidth||test.shell.height!==test.maxHeight){test.fail("maximized geometry not restored");return}
                console.log("REVIEW_UI_RESTORE",test.shell.width,test.shell.height,veyra.paused)
                test.next(5);veyra.takeScreenshot()
            } else if(test.phase===5&&veyra.lastScreenshot.length>0&&test.screenshotNotice.length>0){
                const file=veyra.lastScreenshot.split(/[\\/]/).pop()
                if(test.screenshotNotice.indexOf(file)<0){test.fail("notice and saved file disagree");return}
                console.log("REVIEW_UI_SCREENSHOT",JSON.stringify({file:veyra.lastScreenshot,notice:test.screenshotNotice,color:veyra.colorStatus}))
                console.log("REVIEW_UI_PASS");timer.stop();Qt.quit()
            }
        }
    }
}
