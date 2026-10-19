// One beauty node's parameters, shared by the list page's 美颜 layers and the
// node page's beauty node, so both edit the same values through the same bridge
// calls (as AntiAliasEditor does for anti-aliasing). Two independent sub-functions:
// 人脸原画保护 blends the frame's original back in inside the faces, 磨皮 smooths
// skin while keeping eyes, lashes and lips. The face detection is the chain's,
// shared by every beauty node.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    // veyra.beautyLayers[i] (list) or veyra.beautyParamsAt(index) (node).
    required property var params
    // veyra.beautyDetection and veyra.beautyStatus. The list page draws them once
    // for the whole group (its layers set showDetection false and one editor sets
    // detectionOnly); the node editor shows them in every node.
    property var detection: ({quality: 1, maxFaces: 4, stability: 0.5})
    property string status: ""
    property bool showDetection: true
    property bool detectionOnly: false
    property string namePrefix: "beauty"
    signal edited(string key, double amount)
    signal presetPicked(string preset)
    signal detectionEdited(string key, double amount)
    spacing: 6
    readonly property bool restoreOn: params.restore === true
    readonly property bool smoothOn: params.smooth === true
    readonly property var presets: [
        {id: "natural", label: qsTr("自然磨皮")}, {id: "light", label: qsTr("轻度磨皮")},
        {id: "restore", label: qsTr("人脸原画保护")}, {id: "restoreNatural", label: qsTr("原画保护 + 自然磨皮")}
    ]

    // A 0..1 value shown and typed as a percentage.
    component PercentRow: VRow {
        id: row
        required property string key
        property real from: 0
        property real to: 1
        property real defaultValue: 0
        property real step: 0.01
        value: Math.round(Number(editor.params[key] ?? defaultValue) * 100) + "%"
        VSlider {
            objectName: editor.namePrefix + "-" + row.key
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: row.defaultValue
            from: row.from; to: row.to; inputScale: 100
            value: editor.params[row.key] ?? row.defaultValue
            onMoved: value => editor.edited(row.key, Math.round(value / row.step) * row.step)
        }
    }

    // Every layer row sits in this column, so the shared panel hides them at once.
    ColumnLayout {
        Layout.fillWidth: true
        visible: !editor.detectionOnly
        spacing: 6
        Text {
            Layout.fillWidth: true
            text: qsTr("人脸原画保护：按人脸轮廓混回本帧原画（没有经过任何效果）。磨皮：柔化皮肤，眼睛、眉毛和嘴唇不参与。两项可分别开关。")
            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
        VRow {
            label: qsTr("预设")
            hint: qsTr("一键设定开关和数值，肤色范围与人脸识别保持不变")
            VSelect {
                objectName: editor.namePrefix + "-preset"
                title: qsTr("美颜预设")
                options: editor.presets
                value: qsTr("选择…")
                onPicked: id => editor.presetPicked(id)
            }
        }

        // --- 人脸原画保护 ------------------------------------------------
        VRow {
            label: qsTr("人脸原画保护")
            hint: qsTr("例如调色或增强后脸色不自然时，让人脸回到原画")
            VSwitch {
                objectName: editor.namePrefix + "-restore"
                checked: editor.restoreOn
                onToggled: checked => editor.edited("restore", checked ? 1 : 0)
            }
        }
        PercentRow {
            key: "restoreStrength"; label: qsTr("保护强度"); defaultValue: 1
            visible: editor.restoreOn
        }
        VRow {
            label: qsTr("保护范围")
            hint: qsTr("仅皮肤：头发和背景保留效果")
            visible: editor.restoreOn
            VSeg {
                objectName: editor.namePrefix + "-restoreScope"
                options: [{id: "0", label: qsTr("整张脸")}, {id: "1", label: qsTr("仅皮肤")}]
                current: String(editor.params.restoreScope ?? 0)
                onPicked: id => editor.edited("restoreScope", Number(id))
            }
        }
        PercentRow {
            key: "regionExpand"; label: qsTr("区域扩展"); from: -0.10; to: 0.30; defaultValue: 0.05
            hint: qsTr("相对人脸大小；负值向内收")
            visible: editor.restoreOn
        }
        PercentRow {
            key: "feather"; label: qsTr("边缘羽化"); from: 0; to: 0.20; defaultValue: 0.08
            visible: editor.restoreOn
        }

        // --- 磨皮 --------------------------------------------------------
        VRow {
            label: qsTr("磨皮")
            hint: qsTr("在原画保护之后执行")
            VSwitch {
                objectName: editor.namePrefix + "-smooth"
                checked: editor.smoothOn
                onToggled: checked => editor.edited("smooth", checked ? 1 : 0)
            }
        }
        PercentRow { key: "strength"; label: qsTr("整体强度"); defaultValue: 1; visible: editor.smoothOn }
        PercentRow { key: "smoothAmount"; label: qsTr("磨皮程度"); defaultValue: 0.5; visible: editor.smoothOn }
        VRow {
            label: qsTr("磨皮半径")
            hint: qsTr("1080p 像素，随画面尺寸缩放；越大越柔")
            visible: editor.smoothOn
            value: Number(editor.params.radius ?? 4).toFixed(1)
            VSlider {
                objectName: editor.namePrefix + "-radius"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 4
                from: 0; to: 12
                value: editor.params.radius ?? 4
                onMoved: value => editor.edited("radius", Math.round(value * 2) / 2)
            }
        }
        PercentRow {
            key: "detail"; label: qsTr("细节保护"); defaultValue: 0.65; visible: editor.smoothOn
            hint: qsTr("越高越保留五官和发丝")
        }
        PercentRow {
            key: "texture"; label: qsTr("纹理保留"); defaultValue: 0.3; visible: editor.smoothOn
            hint: qsTr("把一部分皮肤纹理留回来，避免塑料感")
        }
        PercentRow { key: "brighten"; label: qsTr("肤色提亮"); defaultValue: 0; visible: editor.smoothOn }
        VRow {
            label: qsTr("磨皮范围")
            hint: qsTr("全部肤色：不依赖人脸识别，画面中所有肤色区域都会处理")
            visible: editor.smoothOn
            VSeg {
                objectName: editor.namePrefix + "-scope"
                options: [{id: "0", label: qsTr("仅人脸")}, {id: "1", label: qsTr("全部肤色")}]
                current: String(editor.params.scope ?? 0)
                onPicked: id => editor.edited("scope", Number(id))
            }
        }
        VSubGroup {
            objectName: editor.namePrefix + "-skin-group"
            Layout.fillWidth: true
            visible: editor.restoreOn || editor.smoothOn
            label: qsTr("肤色范围 · 高级"); count: 7
            PercentRow { key: "hueCenter"; label: qsTr("色相中心"); defaultValue: 0.08 }
            PercentRow { key: "hueRange"; label: qsTr("色相范围"); from: 0.01; to: 0.5; defaultValue: 0.10 }
            PercentRow { key: "satMin"; label: qsTr("饱和度下限"); defaultValue: 0.08; hint: qsTr("须低于上限") }
            PercentRow { key: "satMax"; label: qsTr("饱和度上限"); defaultValue: 0.80 }
            PercentRow { key: "lumaMin"; label: qsTr("亮度下限"); defaultValue: 0.04; hint: qsTr("夜景或暗部的脸调低") }
            PercentRow { key: "lumaMax"; label: qsTr("亮度上限"); defaultValue: 0.95 }
            PercentRow { key: "softness"; label: qsTr("边界柔和"); defaultValue: 0.35 }
        }
        PercentRow {
            key: "minConfidence"; label: qsTr("人脸置信度"); from: 0.3; to: 0.95; defaultValue: 0.6
            hint: qsTr("低于它的人脸不算；误识别时调高")
            visible: editor.restoreOn || editor.smoothOn
        }
        VRow {
            label: qsTr("显示遮罩")
            hint: qsTr("蓝色为原画保护区域，品红为磨皮区域；导出时自动关闭")
            visible: editor.restoreOn || editor.smoothOn
            VSwitch {
                objectName: editor.namePrefix + "-showMask"
                checked: editor.params.showMask === true
                onToggled: checked => editor.edited("showMask", checked ? 1 : 0)
            }
        }
    }

    // --- 人脸识别（全部美颜节点共用） -----------------------------------------
    VSubGroup {
        objectName: editor.namePrefix + "-detection-group"
        Layout.fillWidth: true
        visible: editor.showDetection
        expanded: editor.detectionOnly
        label: qsTr("人脸识别 · 全部美颜节点共用"); count: 3
        VRow {
            label: qsTr("识别档位")
            hint: editor.detection.quality === 0 ? qsTr("每帧识别 · 320 像素，最省；远处的小脸可能识别不到")
                : editor.detection.quality === 2 ? qsTr("每帧识别 · 640 像素，小脸也能识别")
                : qsTr("每帧识别 · 512 像素")
            VSeg {
                objectName: editor.namePrefix + "-quality"
                options: [{id: "0", label: qsTr("性能")}, {id: "1", label: qsTr("均衡")}, {id: "2", label: qsTr("精细")}]
                current: String(editor.detection.quality ?? 1)
                onPicked: id => editor.detectionEdited("quality", Number(id))
            }
        }
        VRow {
            label: qsTr("最多人脸")
            value: String(editor.detection.maxFaces ?? 4)
            VSlider {
                objectName: editor.namePrefix + "-maxFaces"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 4
                from: 1; to: 8; inputDecimals: 0
                value: editor.detection.maxFaces ?? 4
                onMoved: value => editor.detectionEdited("maxFaces", Math.round(value))
            }
        }
        VRow {
            label: qsTr("跟踪平滑")
            hint: qsTr("越高静止时越稳；人物移动时都会紧跟")
            value: Math.round(Number(editor.detection.stability ?? 0.5) * 100) + "%"
            VSlider {
                objectName: editor.namePrefix + "-stability"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 0.5
                from: 0; to: 1; inputScale: 100
                value: editor.detection.stability ?? 0.5
                onMoved: value => editor.detectionEdited("stability", Math.round(value * 100) / 100)
            }
        }
    }
    Text {
        objectName: editor.namePrefix + "-status"
        Layout.fillWidth: true
        visible: editor.showDetection && editor.status.length > 0
        text: editor.status
        color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
    }
}
