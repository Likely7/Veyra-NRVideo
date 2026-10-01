
    // ---- performance run: capture 2560x1440 60 NV12, NR (1080p layer), NVIDIA flow, XeSS 2X ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) dialogs.open("capture")
            if (ticks === 6) {
                const f = veyra.captureFormats.find(x => x.label.indexOf("2560 x 1440 @ 60") === 0 && x.label.indexOf("NV12") > 0)
                if (f) veyra.captureFormatKey = f.id
                veyra.logUi("uitest", "UITEST_NOTE format=" + (f ? f.label : "not found"))
                veyra.nrEnabled = true
                veyra.fgBackendName = "@FG@"
                veyra.fgEnabled = @FGON@
                veyra.startCaptureSession(); dialogs.close()
            }
            if (ticks === 66) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
