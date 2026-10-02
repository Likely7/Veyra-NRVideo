import QtQuick
import QtQuick.Layouts

// Point curve editor. The engine's samples (ColorGradeTables::curveValue) draw
// the settled curve; while a point is dragged the editor draws its own draft
// with the same monotone Hermite spline, so the line follows the pointer
// instead of waiting for the engine round trip, and edits go out every 40 ms.
ColumnLayout {
    id: root
    property int channel: 0
    property var curves: [[{x:0,y:0},{x:1,y:1}],[{x:0,y:0},{x:1,y:1}],[{x:0,y:0},{x:1,y:1}],[{x:0,y:0},{x:1,y:1}]]
    property var samples: []
    readonly property var modelPoints: curves[channel] || []
    // The draft owns the points during a drag; otherwise the model does.
    property var draft: null
    readonly property var points: draft || modelPoints
    readonly property color channelColor: ["#E7E7EA","#FF5A5A","#4CD964","#4F8BFF"][channel]
    readonly property int maxPoints: 8
    signal edited(int channel, var points)
    spacing: 8
    onPointsChanged: graph.requestPaint()
    onSamplesChanged: graph.requestPaint()
    onChannelChanged: { draft = null; graph.requestPaint() }

    // ColorGradeTables.cpp curveTangents + pointCurve, ported for the live draft.
    function curveAt(pts, x) {
        const n = pts.length
        if (n < 2) return x
        if (x <= pts[0].x) return pts[0].y
        if (x >= pts[n - 1].x) return pts[n - 1].y
        const sec = [], m = []
        for (let i = 0; i < n - 1; ++i) sec.push((pts[i + 1].y - pts[i].y) / Math.max(1e-6, pts[i + 1].x - pts[i].x))
        m[0] = sec[0]
        for (let i = 1; i < n - 1; ++i) m[i] = sec[i - 1] * sec[i] <= 0 ? 0 : (sec[i - 1] + sec[i]) / 2
        m[n - 1] = sec[n - 2]
        for (let i = 0; i < n - 1; ++i) {
            if (sec[i] === 0) { m[i] = 0; m[i + 1] = 0; continue }
            const a = m[i] / sec[i], b = m[i + 1] / sec[i], mag = Math.sqrt(a * a + b * b)
            if (mag > 3) { const k = 3 / mag; m[i] = k * a * sec[i]; m[i + 1] = k * b * sec[i] }
        }
        for (let i = 0; i < n - 1; ++i) {
            const p = pts[i], q = pts[i + 1]
            if (x <= q.x) {
                const dx = Math.max(1e-6, q.x - p.x), t = Math.max(0, Math.min(1, (x - p.x) / dx))
                const t2 = t * t, t3 = t2 * t
                return (2 * t3 - 3 * t2 + 1) * p.y + (t3 - 2 * t2 + t) * dx * m[i] + (-2 * t3 + 3 * t2) * q.y + (t3 - t2) * dx * m[i + 1]
            }
        }
        return pts[n - 1].y
    }
    function clamp01(v) { return Math.max(0, Math.min(1, v)) }

    VSeg {
        objectName: "colour-curve-channels"
        Layout.fillWidth: true
        options: [{id:"0",label:"RGB"},{id:"1",label:qsTr("红")},{id:"2",label:qsTr("绿")},{id:"3",label:qsTr("蓝")}]
        current: String(root.channel)
        onPicked: id => root.channel = Number(id)
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(width, 240)
        color: "#17191E"; radius: 8; border.color: Theme.stroke
        Canvas {
            id: graph
            objectName: "colour-curve-canvas"
            anchors.fill: parent; anchors.margins: 12
            property int hover: -1
            property int active: -1
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onHoverChanged: requestPaint()
            onActiveChanged: requestPaint()
            onPaint: {
                const c = getContext("2d"); c.reset()
                c.strokeStyle = "#30343C"; c.lineWidth = 1
                for (let i = 0; i <= 4; ++i) {
                    c.beginPath(); c.moveTo(width * i / 4, 0); c.lineTo(width * i / 4, height); c.stroke()
                    c.beginPath(); c.moveTo(0, height * i / 4); c.lineTo(width, height * i / 4); c.stroke()
                }
                c.strokeStyle = "#545966"; c.beginPath(); c.moveTo(0, height); c.lineTo(width, 0); c.stroke()
                c.strokeStyle = root.channelColor; c.lineWidth = 2; c.beginPath()
                const values = root.draft ? null : root.samples[root.channel]
                if (values && values.length > 1) {
                    for (let i = 0; i < values.length; ++i) {
                        const x = width * i / (values.length - 1), y = height * (1 - values[i])
                        if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
                    }
                } else {
                    const steps = Math.max(32, Math.round(width / 3))
                    for (let i = 0; i <= steps; ++i) {
                        const x = i / steps, y = root.clamp01(root.curveAt(root.points, x))
                        if (i === 0) c.moveTo(x * width, (1 - y) * height); else c.lineTo(x * width, (1 - y) * height)
                    }
                }
                c.stroke()
                for (let i = 0; i < root.points.length; ++i) {
                    const p = root.points[i], big = i === active || i === hover
                    c.beginPath(); c.arc(p.x * width, (1 - p.y) * height, big ? 7 : 5.5, 0, Math.PI * 2)
                    c.fillStyle = i === active ? root.channelColor : "#17191E"; c.fill()
                    c.lineWidth = 2; c.strokeStyle = root.channelColor; c.stroke()
                }
            }
            // Coalesce drag edits: the engine gets the latest draft every 40 ms and
            // once more on release.
            Timer {
                id: sendTimer
                interval: 40
                onTriggered: if (root.draft) root.edited(root.channel, root.draft.map(p => ({ x: p.x, y: p.y })))
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8     // points on the border stay easy to grab
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                preventStealing: true
                cursorShape: graph.hover >= 0 || graph.active >= 0 ? Qt.SizeAllCursor : Qt.CrossCursor
                readonly property real w: graph.width
                readonly property real h: graph.height
                function local(mouse) { return Qt.point(mouse.x - 8, mouse.y - 8) }
                function closest(x, y) {
                    let found = -1, dist = 16 * 16
                    for (let i = 0; i < root.points.length; ++i) {
                        const p = root.points[i], d = Math.pow(p.x * w - x, 2) + Math.pow((1 - p.y) * h - y, 2)
                        if (d < dist) { found = i; dist = d }
                    }
                    return found
                }
                function moveTo(x, y) {
                    const i = graph.active
                    if (i < 0 || !root.draft || i >= root.draft.length) return
                    const next = root.draft.map(p => ({ x: p.x, y: p.y }))
                    const last = next.length - 1
                    // The end points stay on the edges and only move vertically.
                    const px = i === 0 ? 0 : i === last ? 1
                             : Math.max(next[i - 1].x + 0.01, Math.min(next[i + 1].x - 0.01, x / w))
                    next[i] = { x: px, y: root.clamp01(1 - y / h) }
                    root.draft = next
                    if (!sendTimer.running) sendTimer.start()
                }
                function removeAt(i) {
                    if (i <= 0 || i >= root.points.length - 1) return
                    const next = root.points.map(p => ({ x: p.x, y: p.y })); next.splice(i, 1)
                    draftRelease.stop(); sendTimer.stop(); root.draft = null
                    root.edited(root.channel, next)
                }
                onPositionChanged: mouse => {
                    const p = local(mouse)
                    if (pressed && graph.active >= 0 && (pressedButtons & Qt.LeftButton)) moveTo(p.x, p.y)
                    else graph.hover = closest(p.x, p.y)
                }
                onExited: graph.hover = -1
                onPressed: mouse => {
                    const p = local(mouse)
                    let i = closest(p.x, p.y)
                    if (mouse.button === Qt.RightButton) { removeAt(i); return }
                    root.draft = root.modelPoints.map(q => ({ x: q.x, y: q.y }))
                    if (i < 0) {
                        // A single click on the graph adds a point ON the curve at
                        // that x (so nothing jumps), then drags it straight away.
                        const x = root.clamp01(p.x / w)
                        if (root.draft.length >= root.maxPoints || root.draft.some(q => Math.abs(q.x - x) < 0.01)) {
                            root.draft = null; return
                        }
                        root.draft.push({ x: x, y: root.clamp01(root.curveAt(root.draft, x)) })
                        root.draft.sort((a, b) => a.x - b.x)
                        root.draft = root.draft.slice()
                        i = root.draft.findIndex(q => q.x === x)
                    }
                    graph.active = i
                    moveTo(p.x, p.y)
                }
                function finish() {
                    sendTimer.stop()
                    if (root.draft) root.edited(root.channel, root.draft.map(p => ({ x: p.x, y: p.y })))
                    graph.active = -1
                    // Keep drawing the draft until the engine's samples arrive.
                    draftRelease.restart()
                }
                onReleased: finish()
                onCanceled: finish()
            }
            Timer { id: draftRelease; interval: 250; onTriggered: if (graph.active < 0) root.draft = null }
        }
    }
    RowLayout {
        Text { Layout.fillWidth: true; text: qsTr("单击曲线加点并拖动 · 右键删点 · 最多 8 点"); color: Theme.t3; font.pixelSize: 11 }
        VButton { text: qsTr("还原通道"); ghost: true; onClicked: root.edited(root.channel, [{x:0,y:0},{x:1,y:1}]) }
    }
}
