import QtQuick
import QtTest
import Veyra

// The beauty editor shared by the list layers and the node card: rows follow the
// two sub-function switches, edits leave with the bridge's key names, and the
// shared face detection appears once per list group or in every node.
Item {
    id: root
    width: 520; height: 1400
    readonly property var off: ({index: 4, layer: 0, present: true, enabled: true, restore: false, restoreStrength: 1,
                                 restoreScope: 0, regionExpand: 0.05, feather: 0.08, smooth: false, strength: 1,
                                 smoothAmount: 0.5, radius: 4, detail: 0.65, texture: 0.3, brighten: 0, scope: 0,
                                 hueCenter: 0.08, hueRange: 0.1, satMin: 0.08, satMax: 0.8, lumaMin: 0.04, lumaMax: 0.95,
                                 softness: 0.35, minConfidence: 0.6, showMask: false, summary: ""})
    property var params: off
    property var edits: []
    property var presets: []
    property var detectionEdits: []
    BeautyEditor {
        id: editor
        x: 10; y: 10; width: 480
        params: root.params
        namePrefix: "t-beauty"
        detection: ({quality: 1, maxFaces: 4, stability: 0.5})
        status: "检测到 1 张人脸"
        onEdited: (key, amount) => { const next = root.edits.slice(); next.push({key: key, amount: amount}); root.edits = next }
        onPresetPicked: preset => { const next = root.presets.slice(); next.push(preset); root.presets = next }
        onDetectionEdited: (key, amount) => { const next = root.detectionEdits.slice(); next.push({key: key, amount: amount}); root.detectionEdits = next }
    }
    // The list group's shared panel: created detection-only, as ProPage does.
    BeautyEditor {
        id: shared
        x: 10; y: 900; width: 480
        params: ({})
        namePrefix: "t-shared"
        detectionOnly: true
        status: "未检测到人脸"
    }
    TestCase {
        name: "BeautyEditor"
        when: windowShown
        function init() {
            root.edits = []; root.presets = []; root.detectionEdits = []; root.params = root.off
            editor.showDetection = true; editor.detectionOnly = false
        }
        function withParams(changes) {
            const p = Object.assign({}, root.off)
            for (const k in changes) p[k] = changes[k]
            root.params = p
        }
        function child(name) { const c = findChild(editor, "t-beauty-" + name); verify(c !== null, name); return c }
        function test_both_off_shows_only_switches() {
            compare(child("preset").visible, true)
            compare(child("restore").visible, true)
            compare(child("smooth").visible, true)
            for (const key of ["restoreStrength", "restoreScope", "regionExpand", "feather", "strength", "smoothAmount",
                               "radius", "detail", "texture", "brighten", "scope", "skin-group", "minConfidence", "showMask"])
                compare(child(key).visible, false, key)
            compare(child("detection-group").visible, true)
            compare(child("status").visible, true)
        }
        function test_restore_rows() {
            withParams({restore: true})
            for (const key of ["restoreStrength", "restoreScope", "regionExpand", "feather", "skin-group", "minConfidence", "showMask"])
                compare(child(key).visible, true, key)
            for (const key of ["strength", "smoothAmount", "radius", "detail", "texture", "brighten", "scope"])
                compare(child(key).visible, false, key)
        }
        function test_smooth_rows() {
            withParams({smooth: true, radius: 6.5, scope: 1})
            for (const key of ["strength", "smoothAmount", "radius", "detail", "texture", "brighten", "scope", "skin-group"])
                compare(child(key).visible, true, key)
            compare(child("restoreStrength").visible, false)
            compare(child("radius").value, 6.5)
            compare(child("scope").current, "1")
        }
        function test_switches_send_keys() {
            const restore = child("restore")
            mouseClick(restore, restore.width / 2, restore.height / 2)
            const smooth = child("smooth")
            mouseClick(smooth, smooth.width / 2, smooth.height / 2)
            compare(root.edits.length, 2)
            compare(root.edits[0].key, "restore"); compare(root.edits[0].amount, 1)
            compare(root.edits[1].key, "smooth"); compare(root.edits[1].amount, 1)
        }
        function test_slider_edits_round() {
            withParams({restore: true, smooth: true})
            child("smoothAmount").moved(0.4567)
            child("radius").moved(5.3)
            child("regionExpand").moved(-0.0449)
            compare(root.edits.length, 3)
            compare(root.edits[0].key, "smoothAmount"); fuzzyCompare(root.edits[0].amount, 0.46, 1e-9)
            compare(root.edits[1].key, "radius"); compare(root.edits[1].amount, 5.5)
            compare(root.edits[2].key, "regionExpand"); fuzzyCompare(root.edits[2].amount, -0.04, 1e-9)
        }
        function test_scope_and_preset() {
            withParams({restore: true})
            const scope = child("restoreScope")
            mouseClick(scope, scope.width - 14, scope.height / 2)
            compare(root.edits.length, 1)
            compare(root.edits[0].key, "restoreScope"); compare(root.edits[0].amount, 1)
            child("preset").picked("restoreNatural")
            compare(root.presets, ["restoreNatural"])
        }
        function test_detection_edits() {
            const group = child("detection-group")
            group.expanded = true
            const quality = child("quality")
            mouseClick(quality, quality.width - 14, quality.height / 2)
            child("maxFaces").moved(2.6)
            child("stability").moved(0.333)
            compare(root.detectionEdits.length, 3)
            compare(root.detectionEdits[0].key, "quality"); compare(root.detectionEdits[0].amount, 2)
            compare(root.detectionEdits[1].key, "maxFaces"); compare(root.detectionEdits[1].amount, 3)
            compare(root.detectionEdits[2].key, "stability"); fuzzyCompare(root.detectionEdits[2].amount, 0.33, 1e-9)
            compare(root.edits.length, 0)
            group.expanded = false
        }
        function test_list_layer_and_shared_panel() {
            // A list layer: no detection rows (the group draws them once).
            editor.showDetection = false
            compare(child("detection-group").visible, false)
            compare(child("status").visible, false)
            compare(child("preset").visible, true)
            // The group's shared panel: detection only, opened.
            const part = name => { const c = findChild(shared, "t-shared-" + name); verify(c !== null, name); return c }
            compare(part("preset").visible, false)
            compare(part("restore").visible, false)
            compare(part("detection-group").visible, true)
            compare(part("detection-group").expanded, true)
            compare(part("quality").visible, true)
            compare(part("status").visible, true)
        }
    }
}
