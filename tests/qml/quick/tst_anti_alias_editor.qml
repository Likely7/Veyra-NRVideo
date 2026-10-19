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
        function init() { root.edits = []; root.params = root.smaa }
        function withParams(changes) {
            const p = Object.assign({}, root.smaa)
            for (const k in changes) p[k] = changes[k]
            root.params = p
        }
        function child(name) { const c = findChild(editor, "t-aa-" + name); verify(c !== null, name); return c }
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
            // The rightmost option of a segment sits at its right edge.
            const method = child("method")
            mouseClick(method, method.width - 14, method.height / 2)
            const temporal = child("temporal")
            mouseClick(temporal, temporal.width - 12, temporal.height / 2)
            compare(root.edits.length, 2)
            compare(root.edits[0].key, "method"); compare(root.edits[0].amount, 2)
            compare(root.edits[1].key, "temporal"); compare(root.edits[1].amount, 2)
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
