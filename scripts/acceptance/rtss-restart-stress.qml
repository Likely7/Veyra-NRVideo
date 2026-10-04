import QtQuick
Item {
    id: test
    property var appWindow: Window.window
    property string media: ""
    property string evidence: ""
    property int index: -1
    property int ticks: 0
    property int waited: 0
    property int settling: 0
    property double before: 0
    property bool captured: false
    property int transportPhase: 0
    property int transportTicks: 0
    property var cases: [
        {name:"plain",nr:false,sr:false,fg:"",m:2,q:1},
        {name:"nr-parameters",nr:true,sr:false,fg:"",m:2,q:1},
        {name:"nr-rtx-highest-4k",nr:true,sr:true,fg:"",m:2,q:1},
        {name:"nr-rtx-dlss2",nr:true,sr:true,fg:"dlss",m:2,q:1},
        {name:"dlss6",nr:false,sr:false,fg:"dlss",m:6,q:1},
        {name:"xess4",nr:false,sr:false,fg:"xess",m:4,q:1},
        {name:"fsr3-2",nr:false,sr:false,fg:"fsr3",m:2,q:1},
        {name:"vfg2-low",nr:false,sr:false,fg:"vfg",m:2,q:0},
        {name:"vfg4-medium",nr:false,sr:false,fg:"vfg",m:4,q:1},
        {name:"vfg8-high",nr:false,sr:false,fg:"vfg",m:8,q:2},
        {name:"nr-resize-pause-seek",nr:true,sr:false,fg:"",m:2,q:1}
    ]
    function require(ok, message) {
        if (ok) return
        timer.stop()
        console.error("RTSS_STRESS_FAIL", index, message, veyra.statusText, veyra.backendWarning)
        Qt.quit()
        throw new Error(message)
    }
    function next() {
        ++index; waited=0; settling=0; captured=false; transportPhase=0; transportTicks=0
        if (index === cases.length) {
            console.log("RTSS_STRESS_PASS", cases.length, "version="+veyra.version)
            Qt.quit(); return
        }
        const c=cases[index]
        veyra.fgEnabled=false
        veyra.nrEnabled=c.nr; veyra.srEnabled=c.sr
        veyra.nrIntensity=0.45; veyra.nrTone=0.2; veyra.nrStructure=0.3; veyra.nrSkin=0.25
        veyra.videoSrQuality=4; veyra.srTargetIndex=2
        if (c.fg.length) {
            veyra.fgBackendName=c.fg; veyra.fgMultiplier=c.m; veyra.vfgQuality=c.q
            veyra.fgEnabled=true
        }
        before=veyra.position
        console.log("RTSS_STRESS_REQUEST", c.name)
    }
    Connections {
        target: veyra
        function onOverlayRestartSuggested() { test.require(false,"live server was detected before launch; another restart is invalid") }
        function onNotice(message,error) { console.log("RTSS_STRESS_NOTICE",message,error) }
    }
    Timer {
        id: timer
        interval: 200; repeat: true; running: true
        onTriggered: {
            if (++test.ticks > 1150) { test.require(false,"230-second deadline"); return }
            if (!test.media.length) return
            test.require(vySoftwareUi && veyra.overlayCompatActive && veyra.rivaTunerRunning(),"real RTSS compatibility missing")
            test.require(test.appWindow.color.a === 1,"transparent software background")
            test.require(!veyra.failed,"source/GPU failure")
            if (test.index === -1) {
                if (test.ticks === 5) {
                    test.appWindow.page="pro"; test.appWindow.requestActivate()
                    veyra.muted=true; veyra.nrEnabled=false; veyra.srEnabled=false; veyra.fgEnabled=false
                    veyra.openPath(test.media)
                }
                if (veyra.running && veyra.hasSource && !veyra.applying) test.next()
                return
            }
            const c=test.cases[test.index]
            ++test.waited
            if (test.waited > 140) { test.require(false,"backend transition deadline"); return }
            if (veyra.applying || test.waited < 22 || !veyra.running) return
            if (veyra.nrActive !== c.nr || veyra.srActive !== c.sr || veyra.fgActive !== !!c.fg.length) return
            if (c.fg.length && (veyra.fgBackendName !== c.fg || veyra.fgMultiplier !== c.m)) return
            if (c.name === "nr-resize-pause-seek") {
                // Wait for real state acknowledgements, including a cold NR
                // initialization; transport/window changes are asynchronous.
                if (test.transportPhase === 0) {
                    test.appWindow.width=1100; test.appWindow.height=680
                    veyra.togglePlayPause(); test.transportPhase=1; return
                }
                if (test.transportPhase === 1) {
                    if (!veyra.paused) return
                    veyra.seekTo(2); test.transportPhase=2; return
                }
                if (test.transportPhase === 2) {
                    test.require(veyra.paused,"pause was lost while seeking")
                    if (Math.abs(veyra.position-2)>=0.6) return
                    console.log("RTSS_STRESS_SEEK",veyra.position)
                    veyra.togglePlayPause(); test.before=2
                    test.appWindow.page="min"; test.appWindow.toggleFullscreen()
                    test.transportPhase=3; return
                }
                if (test.transportPhase === 3) {
                    if (!test.appWindow.fullscreen) return
                    if (++test.transportTicks<10) return
                    console.log("RTSS_STRESS_FULLSCREEN",test.appWindow.fullscreen)
                    test.appWindow.toggleFullscreen(); test.appWindow.page="pro"
                    test.transportPhase=4; return
                }
                if (test.transportPhase === 4) {
                    if (test.appWindow.fullscreen) return
                    console.log("RTSS_STRESS_WINDOW_RESTORED",!test.appWindow.fullscreen)
                    test.transportPhase=5
                }
            }
            if (veyra.paused) return
            if (veyra.position < test.before+0.5) return
            if (!test.captured) {
                test.captured=true
                const state={name:c.name,position:veyra.position,detail:veyra.runStatusDetail,
                    nr:veyra.nrActive,sr:veyra.srActive,fg:veyra.fgActive,backend:veyra.fgBackendName,
                    multiplier:veyra.fgMultiplier,quality:veyra.vfgQuality,submitFps:veyra.submitFps,
                    intensity:veyra.nrIntensity,tone:veyra.nrTone,structure:veyra.nrStructure,skin:veyra.nrSkin}
                console.log("RTSS_STRESS_CASE",JSON.stringify(state))
                test.appWindow.contentItem.grabToImage(result=>{
                    test.require(result.saveToFile(test.evidence+"/"+c.name+"-ui.png"),"UI screenshot failed")
                })
                return
            }
            if (++test.settling>8) test.next()
        }
    }
}
