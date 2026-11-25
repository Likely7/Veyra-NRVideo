import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "Ps5Bindings.js" as Bindings
import "Ps5Curve.js" as Curve

ColumnLayout {
    id: page
    required property var settings
    required property string copyName
    spacing: 10
    readonly property var profile: settings ? settings.profile : ({})
    readonly property var contexts: (profile.look || {}).per_context || ({})
    readonly property string profileName: settings ? settings.selected : ""
    property string situation: "on_foot"
    property string axis: "horizontal"
    property int pointIndex: 0
    // Stage only changed axes. Saving always starts from the latest profile.
    property var edits: ({})
    readonly property var measurements: (contexts[situation] || {}).camera_measurements || ({})
    readonly property var horizontal: (edits[situation] || {}).horizontal || measurements.horizontal
    readonly property var vertical: (edits[situation] || {}).vertical || measurements.vertical
    readonly property var selectedCurve: axis === "horizontal" ? horizontal : vertical
    readonly property var point: selectedCurve ? selectedCurve.points[pointIndex] : null
    readonly property var presetCurve: {
        const preset = settings ? settings.presetProfile : null
        if (!preset || preset.game !== profile.game || preset.platform !== profile.platform) return null
        const context = ((preset.look || {}).per_context || {})[situation]
        return context && context.camera_measurements ? context.camera_measurements[axis] : null
    }
    property string fieldError: ""
    readonly property var situations: ["on_foot", "aiming"].filter(s => !!(contexts[s] || {}).camera_measurements)
    readonly property bool knownFit: measurements.fit === undefined || typeof measurements.fit === "boolean"
    readonly property real topSpeed: {
        const last = curve => curve ? curve.points[curve.points.length - 1][1] : 0
        const peak = Math.max(10, last(horizontal), last(vertical))
        const unit = Math.pow(10, Math.floor(Math.log10(peak))) / 2
        return Math.ceil(peak / unit) * unit
    }
    property bool savingCopy: false
    onProfileNameChanged: {
        if (savingCopy) return
        edits = ({})
        pointIndex = 0
        fieldError = ""
    }
    onSituationsChanged: if (!situations.includes(situation)) situation = situations[0] || ""
    onSituationChanged: { pointIndex = 0; fieldError = "" }
    onPointChanged: showPoint()
    Component.onCompleted: showPoint()
    function showPoint() {
        if (!point || !stick || !speed) return
        stick.text = String(Number((point[0] * 100).toPrecision(15)))
        speed.text = String(point[1])
        stick.modified = false
        speed.modified = false
        fieldError = ""
    }

    function stage(curve) {
        const updated = Object.assign({}, edits)
        updated[situation] = Object.assign({}, updated[situation])
        updated[situation][axis] = curve
        edits = updated
    }
    function changePoint(radius, rate) {
        const points = selectedCurve.points.slice()
        points[pointIndex] = [radius, rate]
        stage(Object.assign({}, selectedCurve, {points: points}))
    }
    function applyNumbers() {
        if (!point || (!stick.modified && !speed.modified)) return fieldError === ""
        const radius = stick.modified ? Number(stick.text) / 100 : point[0]
        const rate = speed.modified ? Number(speed.text) : point[1]
        const points = selectedCurve.points
        const prev = pointIndex ? points[pointIndex - 1] : [selectedCurve.deadzone, 0]
        const next = points[pointIndex + 1]
        if (!isFinite(radius) || Math.fround(radius) <= Math.fround(prev[0])
                || (next ? Math.fround(radius) >= Math.fround(next[0]) : radius !== 1)) {
            fieldError = qsTr("摇杆位置须在相邻点之间，最后一点须为 100%。")
        } else if (!isFinite(rate) || rate <= 0 || rate < prev[1]
                   || (next ? rate > next[1] : rate < 10 || rate > 3600)) {
            fieldError = qsTr("转速须为正且不能递减，满杆转速范围为 10 至 3600 °/s。")
        } else {
            changePoint(radius, rate)
            return true
        }
        return false
    }
    function save() {
        if (!applyNumbers()) return
        const next = JSON.parse(JSON.stringify(profile))
        for (const context of Object.keys(edits))
            Object.assign(next.look.per_context[context].camera_measurements, edits[context])
        savingCopy = true
        if (settings.saveCopy(copyName, next)) edits = ({})
        savingCopy = false
    }
    function selectPoint(direction, index) {
        axis = direction
        pointIndex = index
        fieldError = ""
    }

    component Caption: Label {
        color: Theme.t1
        font.family: Theme.fontUi
        wrapMode: Text.Wrap
    }
    component Field: TextField {
        implicitHeight: 34
        Layout.preferredWidth: 132
        selectByMouse: true
        leftPadding: 10
        rightPadding: 10
        font.family: Theme.fontMono
        font.pixelSize: Theme.fsBody
        color: Theme.t1
        background: Rectangle {
            radius: 6
            color: Theme.card2
            border.color: parent.activeFocus ? Theme.accent : Theme.stroke2
        }
    }
    Caption {
        text: qsTr("鼠标灵敏度")
        font.pixelSize: Theme.fsH2
        font.weight: Font.DemiBold
        font.variableAxes: Theme.axesDemiBold
    }
    GridLayout {
        Layout.fillWidth: true
        columns: page.width >= 700 ? 4 : 2
        uniformCellWidths: true
        columnSpacing: 14
        rowSpacing: 12
        ColumnLayout {
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom
            spacing: 5
            Caption { Layout.fillWidth: true; text: qsTr("本机鼠标 DPI"); color: Theme.t2 }
            SpinBox {
                id: dpiSpin
                objectName: "dpi"
                Accessible.name: qsTr("本机鼠标 DPI")
                Layout.fillWidth: true
                implicitHeight: 34
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsBody
                background: Rectangle {
                    radius: 6
                    color: Theme.card2
                    border.color: dpiSpin.activeFocus ? Theme.accent : Theme.stroke2
                }
                component StepIndicator: Rectangle {
                    property bool increment: false
                    readonly property var step: increment ? dpiSpin.up : dpiSpin.down
                    implicitWidth: 32
                    implicitHeight: 32
                    x: increment !== dpiSpin.mirrored ? dpiSpin.width - width - 1 : 1
                    y: 1
                    radius: 5
                    color: step.pressed || step.hovered ? Theme.card3 : "transparent"
                    VIcon {
                        anchors.centerIn: parent
                        name: parent.increment ? "plus" : "minus"
                        size: 14
                        color: parent.enabled ? Theme.t2 : Theme.t3
                    }
                }
                down.indicator: StepIndicator {}
                up.indicator: StepIndicator { increment: true }
                from: 100
                to: 32000
                editable: true
                value: page.settings.dpi
                onValueModified: page.settings.setDpi(value)
            }
        }
        Repeater {
            model: [
                { field: "sensitivity", label: qsTr("视角灵敏度") },
                { field: "aim_sensitivity_multiplier", label: qsTr("瞄准灵敏度") },
                { field: "driving-sensitivity", label: qsTr("驾驶灵敏度") }
            ]
            ColumnLayout {
                id: tuning
                required property var modelData
                readonly property bool driving: modelData.field === "driving-sensitivity"
                readonly property var look: page.profile.look || ({})
                readonly property var context: driving ? "driving"
                        : modelData.field === "sensitivity" && page.contexts.on_foot
                          && page.contexts.on_foot.sensitivity !== undefined ? "on_foot" : ""
                readonly property string field: driving ? "sensitivity" : modelData.field
                readonly property var values: context ? (page.contexts[context] || {}) : look
                readonly property real gain: values[field] === undefined
                        ? (driving ? (look.sensitivity || 1) : 1) : values[field]
                visible: !driving || (page.profile.contexts || []).includes("driving")
                Layout.minimumWidth: 0
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                spacing: 5
                Caption { Layout.fillWidth: true; text: tuning.modelData.label; color: Theme.t2 }
                Field {
                    id: gainField
                    Layout.fillWidth: true
                    objectName: tuning.modelData.field
                    Accessible.name: tuning.modelData.label
                    text: String(tuning.gain)
                    validator: DoubleValidator {
                        bottom: 0.05
                        top: tuning.field === "aim_sensitivity_multiplier" ? 5 : 20
                        locale: "C"
                    }
                    onEditingFinished: {
                        const value = Number(text)
                        if (acceptableInput && value !== tuning.gain) {
                            const next = JSON.parse(JSON.stringify(page.profile))
                            if (tuning.context) {
                                next.look.per_context = next.look.per_context || {}
                                next.look.per_context[tuning.context] = next.look.per_context[tuning.context] || {}
                                next.look.per_context[tuning.context][tuning.field] = value
                            } else {
                                next.look[tuning.field] = value
                            }
                            page.savingCopy = true
                            page.settings.saveCopy(page.copyName, next)
                            page.savingCopy = false
                        }
                        text = Qt.binding(() => String(tuning.gain))
                    }
                }
            }
        }
    }
    Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 4; implicitHeight: 1; color: Theme.stroke2 }
    Caption {
        text: qsTr("相机响应曲线")
        font.pixelSize: Theme.fsH2
        font.weight: Font.DemiBold
        font.variableAxes: Theme.axesDemiBold
    }
    RowLayout {
        visible: page.situations.length > 0
        Ps5EditorPicker {
            id: situationBox
            objectName: "curve-situation"
            Accessible.name: qsTr("当前情境")
            model: page.situations.map(s => Bindings.situationName(page.profile, s))
            currentIndex: page.situations.indexOf(page.situation)
            onActivated: index => page.situation = page.situations[index]
        }
        Item { Layout.fillWidth: true }
        Repeater {
            model: ["horizontal", "vertical"]
            Ps5EditorButton {
                required property string modelData
                text: modelData === "horizontal" ? qsTr("水平") : qsTr("垂直")
                leftPadding: 25
                Rectangle {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: width
                    radius: parent.modelData === "horizontal" ? 4 : 1
                    color: parent.modelData === "horizontal" ? Theme.accent : Theme.exp
                }
                objectName: "curve-axis-" + modelData
                checked: page.axis === modelData
                checkable: true
                onClicked: {
                    const curve = modelData === "horizontal" ? page.horizontal : page.vertical
                    page.selectPoint(modelData, Math.min(page.pointIndex, curve.points.length - 1))
                }
            }
        }
    }
    Caption {
        visible: !page.situations.length
        text: qsTr("此配置没有相机测量点。")
        color: Theme.t2
    }
    ColumnLayout {
        visible: !!page.point
        Layout.fillWidth: true
        spacing: 5
        Caption { text: qsTr("相机转速 (°/s)"); color: Theme.t2 }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: page.width >= 700 ? 230 : 188
            radius: 8
            color: Theme.card2
            Item {
                id: plot
                objectName: "curve-plot"
                anchors.fill: parent
                anchors.leftMargin: 44
                anchors.rightMargin: 14
                anchors.topMargin: 14
                anchors.bottomMargin: 24
                property real dragTop: 0
                readonly property real scaleTop: dragTop || page.topSpeed
                Repeater {
                    model: 5
                    Rectangle {
                        required property int index
                        x: 0
                        y: (1 - index / 4) * plot.height
                        width: plot.width
                        height: 1
                        color: index === 0 ? Theme.stroke2 : Theme.stroke
                        Caption {
                            anchors.right: parent.left
                            anchors.rightMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(Number((plot.scaleTop * parent.index / 4).toFixed(1)))
                            font.pixelSize: 11
                            color: Theme.t2
                        }
                        Caption {
                            x: plot.width * parent.index / 4 - width / 2
                            y: plot.height - parent.y + 4
                            text: String(parent.index * 25)
                            font.pixelSize: 11
                            color: Theme.t2
                        }
                    }
                }
                Repeater {
                    model: ["horizontal", "vertical"]
                    Item {
                        id: series
                        required property string modelData
                        readonly property var curve: modelData === "horizontal" ? page.horizontal : page.vertical
                        readonly property color ink: modelData === "horizontal" ? Theme.accent : Theme.exp
                        readonly property var path: Curve.samples(curve, page.measurements.fit)
                        anchors.fill: parent
                        z: modelData === page.axis ? 1 : 0
                        Canvas {
                            id: lineCanvas
                            anchors.fill: parent
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            Connections {
                                target: series
                                function onPathChanged() { lineCanvas.requestPaint() }
                                function onInkChanged() { lineCanvas.requestPaint() }
                            }
                            Connections {
                                target: plot
                                function onScaleTopChanged() { lineCanvas.requestPaint() }
                            }
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                if (!series.path.length) return
                                ctx.strokeStyle = series.ink
                                ctx.lineWidth = 2
                                ctx.beginPath()
                                for (const [x, y] of series.path)
                                    ctx.lineTo(x * width, (1 - y / plot.scaleTop) * height)
                                ctx.stroke()
                            }
                        }
                        Repeater {
                            model: series.curve ? series.curve.points.length : 0
                            Rectangle {
                                required property int index
                                readonly property var pair: series.curve.points[index]
                                readonly property bool selected: series.modelData === page.axis && page.pointIndex === index
                                objectName: "curve-" + series.modelData + "-" + index
                                x: pair[0] * plot.width - width / 2
                                y: (1 - pair[1] / plot.scaleTop) * plot.height - height / 2
                                width: selected ? 13 : 9
                                height: width
                                radius: series.modelData === "horizontal" ? width / 2 : 2
                                color: selected ? series.ink : Theme.card2
                                border.color: series.ink
                                border.width: 2
                            }
                        }
                    }
                }
                MouseArea {
                    z: 2
                    anchors.fill: parent
                    anchors.margins: -8
                    preventStealing: true
                    property bool dragging: false
                    onPressed: event => {
                        const pos = mapToItem(plot, event.x, event.y)
                        let nearest = 14 * 14, picked = null
                        for (const axis of [page.axis, page.axis === "horizontal" ? "vertical" : "horizontal"]) {
                            const curve = axis === "horizontal" ? page.horizontal : page.vertical
                            if (!curve) continue
                            curve.points.forEach((pair, index) => {
                                const dx = pos.x - pair[0] * plot.width
                                const dy = pos.y - (1 - pair[1] / plot.scaleTop) * plot.height
                                const distance = dx * dx + dy * dy
                                if (distance < nearest) { nearest = distance; picked = [axis, index] }
                            })
                        }
                        if (!picked) { event.accepted = false; return }
                        page.selectPoint(picked[0], picked[1])
                        plot.dragTop = page.topSpeed
                        dragging = true
                    }
                    onPositionChanged: event => {
                        if (!dragging) return
                        const pos = mapToItem(plot, event.x, event.y)
                        const points = page.selectedCurve.points
                        const prev = page.pointIndex ? points[page.pointIndex - 1] : [page.selectedCurve.deadzone, 0]
                        const next = points[page.pointIndex + 1]
                        let radius = next ? Math.max(prev[0], Math.min(next[0], pos.x / plot.width)) : 1
                        if (Math.fround(radius) <= Math.fround(prev[0]) || (next && Math.fround(radius) >= Math.fround(next[0])))
                            radius = page.point[0]
                        const rate = Math.max(next ? prev[1] : Math.max(10, prev[1]),
                                              Math.min(next ? next[1] : 3600, (1 - pos.y / plot.height) * plot.dragTop))
                        if (rate > 0) page.changePoint(radius, rate)
                    }
                    onReleased: { dragging = false; plot.dragTop = 0 }
                    onCanceled: { dragging = false; plot.dragTop = 0 }
                }
            }
        }
        Caption { Layout.alignment: Qt.AlignHCenter; text: qsTr("右摇杆位置 (%)"); color: Theme.t2 }
        Caption {
            visible: !page.knownFit
            text: qsTr("此配置使用未知的拟合模式，仅显示测量点。")
            color: Theme.warn
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            columns: 3
            columnSpacing: 10
            Caption { text: qsTr("测量点"); color: Theme.t2 }
            Caption { text: qsTr("摇杆位置 (%)"); color: Theme.t2 }
            Caption { text: qsTr("转速 (°/s)"); color: Theme.t2 }
            Ps5EditorPicker {
                objectName: "curve-point"
                Accessible.name: qsTr("测量点")
                model: page.selectedCurve ? page.selectedCurve.points.map((_, i) => String(i + 1)) : []
                currentIndex: page.pointIndex
                onActivated: index => page.selectPoint(page.axis, index)
            }
            Field {
                id: stick
                objectName: "curve-stick"
                Accessible.name: qsTr("摇杆位置 (%)")
                property bool modified: false
                onTextEdited: modified = true
                onEditingFinished: page.applyNumbers()
            }
            Field {
                id: speed
                Layout.preferredWidth: 204
                objectName: "curve-speed"
                Accessible.name: qsTr("转速 (°/s)")
                property bool modified: false
                onTextEdited: modified = true
                onEditingFinished: page.applyNumbers()
            }
        }
        Caption {
            Layout.fillWidth: true
            objectName: "curve-error"
            visible: page.fieldError !== ""
            text: page.fieldError
            color: Theme.warn
        }
        RowLayout {
            Layout.topMargin: 4
            Ps5EditorButton {
                objectName: "curve-save"
                text: qsTr("保存到个人配置")
                enabled: Object.keys(page.edits).length > 0 || stick.modified || speed.modified
                primary: enabled
                onClicked: page.save()
            }
            Ps5EditorButton {
                objectName: "curve-restore"
                text: qsTr("恢复此轴的预设曲线")
                quiet: true
                enabled: !!page.presetCurve
                      && JSON.stringify(page.presetCurve) !== JSON.stringify(page.selectedCurve)
                onClicked: page.stage(page.presetCurve)
            }
        }
    }
}
