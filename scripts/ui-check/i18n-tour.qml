    // ---- every page and dialog in one interface language, for layout review ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 900; running: true; repeat: true
        property int ticks: 0
        property int step: 0
        readonly property var plan: [
            ["lang"], ["page", "home"], ["shot", "home"],
            ["open"], ["page", "min"], ["shot", "min"],
            ["page", "pro"], ["shot", "pro"], ["tab", "fg"], ["shot", "pro-fg"], ["tab", "color"], ["shot", "pro-color"], ["tab", "audio"], ["shot", "pro-audio"], ["tab", "display"], ["shot", "pro-display"], ["tab", "quality"],
            ["page", "node"], ["shot", "node"],
            ["page", "exp"], ["shot", "export"],
            ["page", "set"], ["set", "look"], ["shot", "set-look"], ["set", "play"], ["shot", "set-play"],
            ["set", "ps5"], ["shot", "set-ps5"], ["set", "keys"], ["shot", "set-keys"], ["set", "comp"], ["shot", "set-comp"], ["set", "about"], ["shot", "set-about"],
            ["page", "pro"],
            ["dlg", "capture"], ["shot", "dlg-capture"], ["dlg", "ps5"], ["shot", "dlg-ps5"], ["dlg", "moonlight"], ["shot", "dlg-moonlight"],
            ["dlg", "xbox"], ["shot", "dlg-xbox"], ["dlg", "screen"], ["shot", "dlg-screen"], ["dlg", "subtitle"], ["shot", "dlg-subtitle"],
            ["dlg", "audio"], ["shot", "dlg-audio"], ["dlg", "save"], ["shot", "dlg-save"], ["dlg", "manage"], ["shot", "dlg-manage"],
            ["close"], ["done"]
        ]
        onTriggered: {
            ++ticks
            if (ticks < 3) return
            if (step >= plan.length) return
            const a = plan[step++]
            switch (a[0]) {
            case "lang": veyra.setPreference("language", "@LANG@"); veyra.logUi("uitest", "UITEST_NOTE language=" + veyra.uiLanguage); break
            case "page": dialogs.close(); root.goPage(a[1]); break
            case "open": veyra.openUrl("@VIDEO@"); break
            case "set": settingsPage.section = a[1]; break
            case "tab": proPage.tab = a[1]; break
            case "dlg": dialogs.open(a[1]); break
            case "close": dialogs.close(); break
            case "shot": veyra.logUi("uitest", "UITEST_SHOT " + a[1]); break
            case "done": veyra.logUi("uitest", "UITEST_DONE"); running = false; break
            }
        }
    }
