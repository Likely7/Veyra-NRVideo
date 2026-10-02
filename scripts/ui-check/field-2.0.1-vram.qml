    // Real product capture/NR/FG/fullscreen check. No driver settings are changed.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) {
                veyra.logUi("uitest", "UITEST_NOTE screens=" + Qt.application.screens.map(s => s.name + ":" + s.width + "x" + s.height).join(","))
                dialogs.open("capture")
            }
            if (ticks === 6) {
                const f = veyra.captureFormats.find(x => x.label.indexOf("3840 x 2160 @ 60") === 0 && x.label.indexOf("NV12") > 0)
                       || veyra.captureFormats.find(x => x.label.indexOf("2560 x 1440 @ 60") === 0 && x.label.indexOf("NV12") > 0)
                if (!f) { veyra.logUi("uitest", "UITEST_FAIL no 4K/1440p60 NV12 capture format"); running = false; return }
                veyra.captureFormatKey = f.id
                veyra.logUi("uitest", "UITEST_NOTE format=" + f.label)
                veyra.nrEnabled = true
                veyra.fgEnabled = false
                if (!veyra.startCaptureSession()) { veyra.logUi("uitest", "UITEST_FAIL capture start rejected"); running = false; return }
                dialogs.close()
            }
            if (ticks === 40) { veyra.logUi("uitest", "UITEST_NOTE fullscreen NR on FG off"); root.toggleFullscreen() }
            if (ticks === 130) { veyra.logUi("uitest", "UITEST_NOTE fullscreen NR on FG on"); veyra.fgEnabled = true }
            if (ticks === 230) { veyra.logUi("uitest", "UITEST_NOTE fullscreen off"); root.toggleFullscreen() }
            if (ticks === 260) { veyra.logUi("uitest", "UITEST_DONE"); running = false; root.close() }
        }
    }
