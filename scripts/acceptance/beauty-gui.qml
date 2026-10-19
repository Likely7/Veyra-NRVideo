// Loaded into a private copy of Main.qml by beauty-gui.py: drives the real bridge,
// the list page's 美颜 group and the engine while a clip with a face plays.
import QtQuick
import QtQuick.Window
Item {
    id: test
    property string media: ""
    property string shotPath: ""
    property int step: 0
    property int ticks: 0
    property int layerIndex: -1
    property int nodeA: -1
    property int nodeB: -1
    property int waitFaces: 0
    property real beginPosition: 0
    function require(ok, message) {
        if (!ok) throw new Error(message)
        console.log("BEAUTY_UI_CHECK", message)
    }
    function chainTypes() { return veyra.chain.map(n => n.type) }
    function window() { return test.Window.window.contentItem }
    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) { const r = find(item.children[i], name); if (r) return r }
        return null
    }
    // Scroll the inspector so `item` sits at its top.
    function reveal(item) {
        let flick = item.parent
        while (flick && flick.contentY === undefined) flick = flick.parent
        if (!flick) return
        const y = item.mapToItem(flick.contentItem, 0, 0).y - 4
        flick.contentY = Math.max(0, Math.min(y, Math.max(0, flick.contentHeight - flick.height)))
    }
    Timer {
        interval: 400; repeat: true; running: true
        onTriggered: { try {
            if (++test.ticks > 150) throw new Error("deadline step=" + test.step)
            if (test.step === 0) {
                if (test.ticks < 4) return
                test.require(veyra.beautyLayers.length === 0 && !veyra.beautyAnyEnabled, "a fresh profile has no beauty layer")
                const ids = veyra.effectCatalog.map(e => e.id)
                test.require(ids.indexOf("beauty") >= 0 && ids.indexOf("aa") >= 0, "the catalog offers beauty next to anti-aliasing")
                const stage = test.find(test.window(), "list-beauty-stage")
                test.require(stage && stage.visible && !stage.enabledSwitch, "the 美颜 group sits on the 画质 tab, without a switch while empty")
                veyra.volume = 0
                veyra.openPath(test.media)
                test.step = 1; return
            }
            test.require(!veyra.failed, "player has no error")
            if (test.step === 1) {
                if (!veyra.running || veyra.position < 0.5 || veyra.applying) return
                test.beginPosition = veyra.position
                veyra.logUi("beauty-gui", "BEGIN beauty edits")
                // Open the group, as a user does to reach its add button.
                test.find(test.window(), "list-beauty-stage").open = true
                test.step = 11; return
            }
            if (test.step === 11) {
                // The group's dashed add button, as a click sends it.
                const add = test.find(test.window(), "list-beauty-add")
                test.require(add && add.visible && !add.full, "the add button is offered")
                add.add()
                test.step = 2; return
            }
            if (veyra.applying) return
            if (test.step === 2) {
                test.require(veyra.beautyLayers.length === 1, "the add button creates one layer")
                const l = veyra.beautyLayers[0]
                test.layerIndex = l.index
                test.require(!l.enabled && !l.restore && !l.smooth, "a new layer arrives switched off with both sub-functions off")
                const types = test.chainTypes()
                const hdr = types.indexOf("video-hdr")
                test.require(hdr < 0 || types.indexOf("beauty") < hdr, "the layer sits in front of RTX Video HDR")
                test.require(test.find(test.window(), "beauty-card-1") !== null, "the layer card exists")
                test.require(veyra.applyBeautyPreset(test.layerIndex, "restoreNatural"), "preset: original restore + natural smoothing")
                test.step = 3; return
            }
            if (test.step === 3) {
                const l = veyra.beautyParamsAt(test.layerIndex)
                test.require(l.enabled && l.restore && l.smooth && Math.abs(l.smoothAmount - 0.6) < 1e-6 && l.radius === 5 &&
                             Math.abs(l.restoreStrength - 1) < 1e-6, "the preset switches the layer and both sub-functions on")
                test.require(veyra.chain.find(n => n.type === "beauty").enabled, "the chip shows the layer on")
                test.step = 4; return
            }
            if (test.step === 4) {
                // The shared tracker reports the face within a few detections.
                if (veyra.beautyStatus.indexOf("检测到") !== 0) {
                    if (++test.waitFaces < 30) return
                    throw new Error("no face reported: '" + veyra.beautyStatus + "'")
                }
                test.require(true, "status: " + veyra.beautyStatus)
                const rows = veyra.stageTimings.filter(r => r.label === "美颜" || r.label === "人脸识别")
                console.log("BEAUTY_UI_INFO timing", JSON.stringify(rows.map(r => ({label: r.label, ms: r.ms, measured: r.measured}))))
                veyra.logUi("beauty-gui", "BEGIN live beauty values")
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "smoothAmount", 0.8) &&
                             Math.abs(veyra.beautyParamsAt(test.layerIndex).smoothAmount - 0.8) < 1e-6, "the smoothing amount is live")
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "restoreStrength", 0.5), "the restore strength is live")
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "scope", 1) && veyra.beautyParamsAt(test.layerIndex).scope === 1, "all-skin scope")
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "scope", 0), "back to faces")
                test.require(!veyra.setBeautyParameterAt(test.layerIndex, "radius", 13), "an out-of-range radius is refused")
                test.require(!veyra.setBeautyParameterAt(test.layerIndex, "satMin", 0.9), "a saturation floor above the ceiling is refused")
                test.require(veyra.setBeautyDetection("quality", 2) && veyra.beautyDetection.quality === 2, "the detection level is live")
                test.require(veyra.setBeautyDetection("stability", 0.3) && Math.abs(veyra.beautyDetection.stability - 0.3) < 1e-6,
                             "the tracking stability is live")
                test.step = 5; return
            }
            if (test.step === 5) {
                veyra.logUi("beauty-gui", "END live beauty values")
                test.require(veyra.beautyStatus.length > 0, "the status stays: " + veyra.beautyStatus)
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "showMask", 1), "mask view on")
                test.step = 6; return
            }
            if (test.step === 6) {
                test.require(veyra.setBeautyParameterAt(test.layerIndex, "showMask", 0), "mask view off")
                const copy = veyra.duplicateBeautyLayer(test.layerIndex)
                test.require(copy === test.layerIndex + 1 && veyra.beautyLayers.length === 2, "copy as a new layer right behind it")
                test.require(veyra.setBeautyParameterAt(copy, "restoreStrength", 0.25), "the copy edits its own values")
                test.require(veyra.moveBeautyLayer(copy, -1), "move the copy up")
                test.require(Math.abs(veyra.beautyLayers[0].restoreStrength - 0.25) < 1e-6 &&
                             Math.abs(veyra.beautyLayers[1].restoreStrength - 0.5) < 1e-6, "the layers traded places")
                test.require(!veyra.moveBeautyLayer(veyra.beautyLayers[0].index, -1), "the first layer cannot move up")
                test.step = 7; return
            }
            if (test.step === 7) {
                test.require(veyra.setBeautyParameterAt(veyra.beautyLayers[1].index, "enabled", 0), "switch layer 2 off")
                test.require(veyra.setAllBeautyEnabled(false) && !veyra.beautyAnyEnabled, "the group switch turns every layer off")
                test.step = 8; return
            }
            if (test.step === 8) {
                test.require(veyra.setAllBeautyEnabled(true), "group switch on again")
                test.require(veyra.beautyLayers[0].enabled && !veyra.beautyLayers[1].enabled, "on again brings back only the layer that was on")
                test.require(veyra.resetBeautyLayer(veyra.beautyLayers[1].index) && !veyra.beautyLayers[1].restore &&
                             !veyra.beautyLayers[1].smooth, "reset turns the sub-functions off")
                test.require(veyra.removeEffect(veyra.beautyLayers[1].index) && veyra.beautyLayers.length === 1, "remove the second layer")
                test.step = 9; return
            }
            if (test.step === 9) {
                const stage = test.find(test.window(), "list-beauty-stage")
                const card = test.find(test.window(), "beauty-card-1")
                test.require(stage && card, "the group and its layer card")
                stage.open = true; card.open = true
                test.step = 91; return
            }
            if (test.step === 91) {
                test.reveal(test.find(test.window(), "list-beauty-stage"))
                test.step = 92; return
            }
            if (test.step === 92) {
                // grabToImage renders on a later frame: change nothing until it is saved.
                if (test.shotPath.length > 0)
                    test.window().grabToImage(r => r.saveToFile(test.shotPath))
                test.step = 93; return
            }
            if (test.step === 93) {
                veyra.nodeMode = 1
                test.step = 10; return
            }
            if (test.step === 10) {
                test.nodeA = veyra.addEffect("beauty")
                test.require(test.nodeA >= 0 && !veyra.beautyParamsAt(test.nodeA).enabled, "a node-mode beauty node arrives switched off")
                test.require(veyra.applyBeautyPreset(test.nodeA, "natural") && veyra.beautyParamsAt(test.nodeA).enabled &&
                             veyra.beautyParamsAt(test.nodeA).smooth && !veyra.beautyParamsAt(test.nodeA).restore, "the node takes a preset")
                test.require(veyra.duplicateBeautyLayer(test.nodeA) < 0, "node mode copies through the node menu instead")
                // An anti-aliasing node switched off at its defaults has no settings instance either.
                test.nodeB = veyra.addEffect("aa")
                test.require(test.nodeB >= 0 && veyra.setEffectEnabled(test.nodeB, false) && !veyra.antiAliasParamsAt(test.nodeB).enabled,
                             "a node-mode anti-aliasing node switches off at its defaults")
                test.step = 12; return
            }
            if (test.step === 12) {
                test.require(veyra.removeEffect(Math.max(test.nodeA, test.nodeB)) && veyra.removeEffect(Math.min(test.nodeA, test.nodeB)),
                             "remove the nodes")
                veyra.nodeMode = 0
                test.step = 13; return
            }
            test.require(veyra.nodeMode === 0 && veyra.beautyLayers.length === 1 && veyra.beautyLayers[0].enabled,
                         "list mode keeps its own beauty layer")
            test.require(veyra.position > test.beginPosition + 2, "media time advances through the edits")
            veyra.logUi("beauty-gui", "END beauty edits")
            console.log("BEAUTY_UI_PASS")
            Qt.quit()
        } catch(e) { console.log("BEAUTY_UI_FAIL", String(e)); Qt.quit() } }
    }
}
