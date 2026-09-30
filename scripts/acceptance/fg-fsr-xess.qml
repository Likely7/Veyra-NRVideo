    // Inject only into a disposable staged Main.qml, never the product source.
    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        property int current: 0
        property double beforePosition: 0
        property bool restore: Qt.application.arguments.indexOf("--fg-restore") >= 0
        property var cases: [
            {backend:"fsr3", multiplier:2}, {backend:"xess", multiplier:4},
            {backend:"xess", multiplier:2}, {backend:"xess", multiplier:3},
            {backend:"xess", multiplier:4}, {backend:"dlss", multiplier:4},
            {backend:"fsr3", multiplier:2}
        ]
        function check(value, label) { if (!value) throw new Error(label) }
        function next(value) { step = value; ticks = 0 }
        function preset(name) {
            for (var i=0;i<veyra.presets.length;++i)
                if (veyra.presets[i].name === name) return i
            return -1
        }
        function startCase() {
            var c=cases[current]
            beforePosition=veyra.position
            veyra.fgBackendName=c.backend
            veyra.fgEnabled=true
            veyra.fgMultiplier=c.multiplier
            next(2)
        }
        onTriggered: { try {
            ++ticks; ++total
            if (total>680) throw new Error("bounded step="+step+" ticks="+ticks)
            if (step===0) {
                if (ticks<8) return
                check(veyra.fgBackendChoices.length===4, "four independent backend choices")
                if (restore) {
                    check(veyra.nodeMode===0 && veyra.fgBackendName==="fsr3", "list session restored")
                    check(preset("FG List FSR31")>=0 && preset("FG Node FSR31")>=0 && preset("FG Node FSR4")>=0, "preset library restored")
                    veyra.nodeMode=1
                    check(veyra.applyPresetIndex(preset("FG Node FSR4")), "FSR4 node preset applies")
                    check(veyra.fgBackendName==="fsr4" && veyra.fgMaxMultiplier===2, "FSR4 restored without DLSS label")
                    check(veyra.applyPresetIndex(preset("FG Node FSR31")), "FSR3 node preset applies")
                    check(veyra.fgBackendName==="fsr3", "independent FSR3 restored")
                    veyra.nodeMode=0
                    check(veyra.fgBackendName==="fsr3", "list configuration survives node choices")
                    console.log("FG_UI_RESTORE_PASS"); Qt.quit(); return
                }
                veyra.muted=true; veyra.nrEnabled=false; veyra.srEnabled=false
                root.page="pro"
                veyra.openPath("E:/项目/Veyra/tests/nr-fg-opt-20260929/media/gta6-4k30-silent-180s.mp4")
                next(1); return
            }
            if (step===1) {
                if (!veyra.running || !veyra.hasSource || veyra.applying) return
                startCase(); return
            }
            if (step===2) {
                if (ticks<20 || veyra.applying) return
                var c=cases[current]
                check(veyra.running && veyra.fgActive, "FG inactive "+c.backend+" "+veyra.statusText)
                check(veyra.fgBackendName===c.backend && veyra.fgMultiplier===c.multiplier, "backend/multiplier not applied")
                check(veyra.position>beforePosition+0.5, "source position stopped "+c.backend)
                if (c.backend==="fsr3") {
                    check(veyra.fgProviderText.indexOf("3.1.")>=0, "actual FSR3 provider absent")
                    check(veyra.runStatusDetail.indexOf("FSR 3.1")>=0 && veyra.runStatusDetail.indexOf("DLSS")<0, "FSR status mislabeled")
                    check(veyra.fgMultiplierChoices.length===1 && veyra.fgMaxMultiplier===2, "FSR2X ceiling")
                    var old=veyra.fgMultiplier
                    veyra.fgMultiplier=4
                    check(veyra.fgMultiplier===old, "illegal FSR4X mutated settings")
                }
                if (c.backend==="xess") check(veyra.fgMaxMultiplier===4 && veyra.fgMultiplierChoices.length===3, "XeSS 4X option disappeared")
                console.log("FG_UI_CASE backend="+c.backend+" multiplier="+c.multiplier+" fps="+veyra.submitFps.toFixed(1)+" position="+veyra.position.toFixed(2)+" provider="+veyra.fgProviderText)
                ++current
                if (current<cases.length) { startCase(); return }
                check(veyra.savePresetAs("FG List FSR31",15,false), "save list FSR3 preset")
                beforePosition=veyra.position; veyra.fgBackendName="fsr4"
                next(3); return
            }
            if (step===3) {
                if (ticks<20 || veyra.applying) return
                check(veyra.running && veyra.fgActive && veyra.fgBackendName==="fsr3", "unsupported FSR4 did not restore FSR3 backend="+veyra.fgBackendName+" active="+veyra.fgActive+" running="+veyra.running)
                check(veyra.backendWarning.indexOf("FSR 4")>=0, "FSR4 failure not explained")
                check(veyra.position>beforePosition+0.5, "FSR4 rejection stopped playback")
                console.log("FG_UI_FSR4_ROLLBACK warning="+veyra.backendWarning)
                veyra.togglePlayPause(); next(4); return
            }
            if (step===4) {
                if (!veyra.paused) return
                veyra.seekTo(40); root.width=1160; root.height=740
                next(5); return
            }
            if (step===5) {
                if (ticks<12) return
                check(veyra.paused && veyra.position>=39, "paused seek not complete")
                veyra.togglePlayPause(); next(6); return
            }
            if (step===6) {
                if (ticks<16 || veyra.applying) return
                check(veyra.running && !veyra.paused && veyra.fgActive && veyra.position>40.5, "pause/seek/resize/resume failed")
                veyra.nodeMode=1; root.page="node"
                next(7); return
            }
            if (step===7) {
                if (ticks<12 || veyra.applying) return
                check(veyra.effectCatalog.some(function(o){return o.id==="fsr3-fg"}) &&
                      veyra.effectCatalog.some(function(o){return o.id==="fsr4-fg"}), "FSR node add menu absent")
                check(veyra.addEffect("fsr3-fg")>=0, "add FSR3 node failed")
                next(8); return
            }
            if (step===8) {
                if (ticks<20 || veyra.applying) return
                check(veyra.fgActive && veyra.fgBackendName==="fsr3" && veyra.chainValid, "node FSR3 not active")
                check(veyra.chain.some(function(o){return o.type==="frame-generation" && o.label.indexOf("FSR 3.1")>=0}), "node mislabeled")
                check(veyra.savePresetAs("FG Node FSR31",15,true), "save node FSR3 preset")
                veyra.stopPlayback(); next(9); return
            }
            if (step===9) {
                if (ticks<16 || veyra.hasSource) return
                veyra.fgBackendName="fsr4"
                check(veyra.fgBackendName==="fsr4", "idle FSR4 selection failed")
                check(veyra.savePresetAs("FG Node FSR4",15,true), "save FSR4 node preset failed")
                check(veyra.applyPresetIndex(preset("FG Node FSR31")), "restore FSR3 node preset")
                veyra.nodeMode=0; root.page="pro"
                check(veyra.fgBackendName==="fsr3", "list state was overwritten by node FSR4")
                next(10); return
            }
            if (step===10) {
                if (ticks<8) return
                console.log("FG_UI_PASS"); Qt.quit()
            }
        } catch(e) { console.warn("FG_UI_FAIL "+e.message); Qt.quit() } }
    }
