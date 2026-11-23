    // ---- live launch + perf HUD shortcut test ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property bool launched: false
        property bool hudToggled: false
        onTriggered: {
            ++ticks
            if (ticks < 4) return
            if (!launched) {
                if (!veyra.xbox.state.signedIn) { veyra.logUi("uitest", "UITEST_FAIL not signed in"); running = false; return }
                const ok = veyra.xbox.resumeLast()
                veyra.logUi("uitest", "UITEST_NOTE resumeLast=" + ok)
                if (!ok) { veyra.logUi("uitest", "UITEST_FAIL no last console"); running = false; return }
                launched = true
                return
            }
            const st = veyra.xbox.state
            const ss = String((st.stream || {}).state || "")
            if (ss === "streaming" && !hudToggled) {
                hudToggled = true
                veyra.logUi("uitest", "UITEST_NOTE streaming=true statsVisible(before)=" + veyra.moonlightStatsVisible)
                // simulate the shortcut activation path
                veyra.moonlightStatsVisible = !veyra.moonlightStatsVisible
                veyra.logUi("uitest", "UITEST_NOTE statsVisible(after)=" + veyra.moonlightStatsVisible)
                veyra.logUi("uitest", "UITEST_SHOT hud-on")
                return
            }
            if (hudToggled && ticks % 3 === 0) {
                const hud = (function find(it){ if(!it) return null; if(it.objectName==="stream-hud") return it; const k=it.children||[]; for(let i=0;i<k.length;++i){const f=find(k[i]); if(f) return f} return null })(root.contentItem)
                veyra.logUi("uitest", "UITEST_NOTE hud visible=" + (hud ? hud.visible : "NOTFOUND") + " shown=" + (hud ? hud.shown : "?"))
                veyra.logUi("uitest", "UITEST_DONE")
                running = false
            }
        }
    }