import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: row
    property string label: ""
    property string hint: ""
    property color hintColor: Theme.t3
    property string value: ""
    // Narrow panels with a wide control (导出 输出设置): the hint runs under the
    // whole row instead of being squeezed beside the control.
    property bool hintBelow: false
    // Uses the children's declared widths, not childrenRect: the latter is a
    // layout result, and reading it here made `stacked` depend on the layout it
    // changes (Qt reported a binding loop and logged it on every row).
    // Deliberately compares the LABEL only. Counting the hint as well stacked
    // rows whose hint is merely long (补帧方式, VFG 质量) and pushed their control
    // under the label, which is not how those rows looked before (2026-10-24
    // regression). A long hint belongs to the row that has it: pass hintBelow
    // there instead of widening this rule.
    readonly property real labelNeed: Math.min(170, labelMetrics.advanceWidth(label))
    readonly property bool stacked: width > 0 && width - slot.declaredWidth - (value.length ? 52 : 0) - 12
        < labelNeed
    FontMetrics { id: labelMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
    default property alias control: slot.data
    // The row's slider, when its control is one (typed values beside the number).
    readonly property Item valueSlider: {
        for (let i = 0; i < slot.children.length; ++i) {
            const c = slot.children[i]
            if (c && c.commitValue !== undefined && c.inputDecimals !== undefined) return c
        }
        return null
    }

    // The layout reads Layout.preferredHeight. `height` is deliberately NOT set: a
    // layout-managed item that sets its own height is undefined behaviour, and this
    // row is only ever placed inside a layout.
    // A hint that wraps (narrow panels) grows the row instead of running into
    // the next one.
    implicitHeight: Math.max(Theme.rowMinHeight, mainRow.implicitHeight + 8)
        + (hintBelow && hint.length > 0 ? belowHint.implicitHeight + 6 : 0)
    Layout.preferredHeight: implicitHeight
    Layout.fillWidth: true
    GridLayout {
        id: mainRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        columns: row.stacked ? 2 : 3
        columnSpacing: 12
        rowSpacing: 6
        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.columnSpan: row.stacked ? 2 : 1
            spacing: 2
            // fillWidth here too: without a hint the label is the column's only child,
            // and a layout with no filling child cannot grow, which left the control
            // beside the label instead of at the row's right edge.
            // Grow the row for long labels instead of hiding translated text.
            // The minimum comes from the Text's OWN implicitWidth, not from
            // FontMetrics.advanceWidth: the two disagree by a couple of pixels, and
            // a floor computed the other way left a 26px label in 24px of space,
            // which wrapped 倍率 into one character per line (2026-10-24 probe).
            // A long translated label still wraps above that floor instead of
            // being cut off.
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: implicitWidth
                text: row.label
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                wrapMode: Text.Wrap
            }
            Text {
                visible: row.hint.length > 0 && !row.hintBelow
                text: row.hint
                color: row.hintColor
                font.family: Theme.fontUi
                font.pixelSize: 11
                // WordWrap, not Wrap: in a narrow label column Wrap breaks inside
                // words, which turned a long hint into one character per line
                // (2026-10-24, the 倍率 row). WordWrap keeps whole words together;
                // rows whose hint still needs many lines set hintBelow instead.
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Item {
            id: slot
            // Sum of the children's declared widths — stable across layout changes.
            readonly property real declaredWidth: {
                let w = 0
                for (let i = 0; i < children.length; ++i) {
                    const c = children[i]
                    if (!c || !c.visible) continue
                    w += c.implicitWidth > 0 ? c.implicitWidth : 0
                }
                return w
            }
            // Width from the VISIBLE children only. childrenRect counts every child,
            // hidden ones included, so a row holding two alternative controls
            // (倍率: VSeg for DLSS, VSelect for VFG) sized itself to the hidden one
            // and pushed the visible control off its column: 304px where the other
            // rows use 150px (2026-10-24 probe).
            readonly property real visibleWidth: {
                let w = 0
                for (let i = 0; i < children.length; ++i) {
                    const c = children[i]
                    if (!c || !c.visible) continue
                    w = Math.max(w, c.x + (c.implicitWidth > 0 ? c.implicitWidth : c.width))
                }
                return w
            }
            implicitHeight: childrenRect.height
            Layout.preferredWidth: visibleWidth
            Layout.preferredHeight: childrenRect.height
        }
        // A row whose control is a slider: its number can be clicked and typed.
        VSliderValue {
            visible: row.value.length > 0
            text: row.value
            slider: row.valueSlider
            Layout.minimumWidth: 40
        }
    }
    Text {
        id: belowHint
        visible: row.hintBelow && row.hint.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: mainRow.bottom
        anchors.topMargin: 6
        text: row.hint
        color: row.hintColor
        font.family: Theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
