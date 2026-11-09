import QtQuick
import QtTest
import Veyra

// The anti-aliasing editor (#53) shared by the list card and the node card: the
// rows follow the method, and edits leave with the bridge's key names.
Item {
    id: root
    width: 520; height: 560
    readonly property var smaa: ({index: 3, present: true, enabled: true, method: 1, quality: 2, mlaaThreshold: 0.08,
                                  mlaaStrength: 1, temporal: 0, historyWeight: 0.5, historyRejection: 8, summary: "SMAA"})
    property var params: smaa
    property var edits: []
    AntiAliasEditor {
        id: editor
        x: 10; y: 10; width: 480
        params: root.params
        namePrefix: "t-aa"
        onEdited: (key, amount) => { const next = root.edits.slice(); next.push({key: key, amount: amount}); root.edits = next }
    }
    TestCase {
        name: "AntiAliasEditor"
        when: windowShown
        property var savedCapabilities
        function init() { savedCapabilities = veyra.effectCapabilities; root.edits = []; root.params = root.smaa }
        function cleanup() { veyra.effectCapabilities = savedCapabilities }
        function withParams(changes) {
            const p = Object.assign({}, root.smaa)
            for (const k in changes) p[k] = changes[k]
            root.params = p
        }
        function child(name) { const c = findChild(editor, "t-aa-" + name); verify(c !== null, name); return c }
        // One option of a segmented control, by its id.
        function option(seg, id) {
            const visit = item => {
                if (item.modelData !== undefined && item.modelData !== null && item.modelData.id === id) return item
                for (let i = 0; i < item.children.length; ++i) { const f = visit(item.children[i]); if (f) return f }
                return null
            }
            const found = visit(seg); verify(found !== null, "option " + id); return found
        }
        function test_smaa_rows() {
            compare(child("quality").visible, true)
            compare(child("quality").options.length, 4)
            compare(child("quality").current, "2")
            compare(child("temporal").visible, true)
            compare(child("mlaa-threshold").visible, false)
            compare(child("history-weight").visible, false)
            withParams({temporal: 1})
            compare(child("history-weight").visible, true)
            compare(child("history-rejection").visible, true)
        }
        function test_fxaa_rows() {
            withParams({method: 0, quality: 1})
            compare(child("quality").options.length, 3)
            compare(child("temporal").visible, false)
            compare(child("mlaa-strength").visible, false)
        }
        function test_mlaa_rows() {
            withParams({method: 2, quality: 0, mlaaThreshold: 0.12, mlaaStrength: 0.6})
            compare(child("quality").visible, false)
            compare(child("mlaa-threshold").visible, true)
            compare(child("mlaa-strength").visible, true)
            compare(child("mlaa-threshold").value, 0.12)
            compare(child("temporal").visible, false)
        }
        function test_clicks_send_bridge_keys() {
            // MLAA is the third method; the rightmost temporal option sits at its right edge.
            const mlaa = option(child("method"), "2")
            mouseClick(mlaa, mlaa.width / 2, mlaa.height / 2)
            const temporal = child("temporal")
            mouseClick(temporal, temporal.width - 12, temporal.height / 2)
            compare(root.edits.length, 2)
            compare(root.edits[0].key, "method"); compare(root.edits[0].amount, 2)
            compare(root.edits[1].key, "temporal"); compare(root.edits[1].amount, 2)
        }
        // DLAA (2026-11-08): no tiers, no MLAA or SMAA rows.
        function test_dlaa_rows() {
            withParams({method: 3, quality: 0})
            compare(child("quality").visible, false)
            compare(child("temporal").visible, false)
            compare(child("mlaa-threshold").visible, false)
            compare(child("mlaa-strength").visible, false)
            compare(child("method").current, "3")
        }
        // The DLAA option follows DLSS SR's availability (same runtime and adapters).
        function test_dlaa_option_follows_dlss() {
            compare(option(child("method"), "3").enabled, false)
            const caps = Object.assign({}, savedCapabilities); caps.sr0 = {available: true, reason: ""}
            veyra.effectCapabilities = caps
            const dlaa = option(child("method"), "3")
            compare(dlaa.enabled, true)
            mouseClick(dlaa, dlaa.width / 2, dlaa.height / 2)
            compare(root.edits.length, 1)
            compare(root.edits[0].key, "method"); compare(root.edits[0].amount, 3)
        }
        function test_slider_edits_round() {
            withParams({method: 2, quality: 0})
            child("mlaa-threshold").moved(0.1234)
            child("mlaa-strength").moved(0.4321)
            compare(root.edits.length, 2)
            compare(root.edits[0].key, "mlaaThreshold"); compare(root.edits[0].amount, 0.12)
            compare(root.edits[1].key, "mlaaStrength"); compare(root.edits[1].amount, 0.45)
        }
    }
}
