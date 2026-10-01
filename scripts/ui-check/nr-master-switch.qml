    // ---- list-mode NR master switch: all layers off at once, back to what was on ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property string failure: ""
        function states() { return veyra.nrLayers.map(l => l.enabled ? 1 : 0).join("") }
        function expect(what, want) {
            const got = states()
            veyra.logUi("uitest", "UITEST_NOTE " + what + " layers=" + got + " any=" + veyra.nrAnyEnabled)
            if (got !== want && !failure) failure = what + " got " + got + " want " + want
        }
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 5) {
                root.goPage("pro")
                while (veyra.nrLayers.length < 3) veyra.addEffect("nr")
                const l = veyra.nrLayers
                veyra.setEffectEnabled(l[0].index, true); veyra.setEffectEnabled(l[1].index, false); veyra.setEffectEnabled(l[2].index, true)
                expect("start", "101")
            }
            if (ticks === 7) veyra.logUi("uitest", "UITEST_SHOT on")
            if (ticks === 8) { veyra.setAllNrEnabled(false); expect("master off", "000") }
            if (ticks === 10) veyra.logUi("uitest", "UITEST_SHOT off")
            if (ticks === 11) { veyra.setAllNrEnabled(true); expect("master on restores", "101") }
            if (ticks === 12) {
                veyra.setAllNrEnabled(false); veyra.setEffectEnabled(veyra.nrLayers[1].index, true)
                // A layer switched on by hand while the master was off: master shows on.
                expect("one by hand", "010")
                veyra.setAllNrEnabled(false); veyra.setAllNrEnabled(true); expect("restore after hand edit", "010")
            }
            if (ticks === 13) {
                veyra.setAllNrEnabled(false); veyra.addEffect("nr")
                veyra.setAllNrEnabled(true); expect("layer added while off: all on", "1111")
            }
            if (ticks === 14) { veyra.logUi("uitest", failure ? "UITEST_FAIL " + failure : "UITEST_DONE"); running = false }
        }
    }
