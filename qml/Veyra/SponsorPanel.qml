// 设置 → 赞助作者: the author's pages, then Ko-fi, then the WeChat / Alipay codes for
// users in mainland China (the order the author asked for, 2026-10-19).
//
// The page names a link by id only; the bridge owns the addresses
// (QmlPlayerBridge::openSponsorLink), so no URL is shown or editable here.
// Brand tiles are SVGs made by tools/qt_probe/make-sponsor-icons.py from Simple
// Icons (CC0). The QR images are the author's own codes, rounded in the pixels:
// the software renderer cannot run a shader mask.
import QtQuick
import QtQuick.Layouts
import QtQuick.Window

ColumnLayout {
    id: panel
    signal openLink(string id)
    spacing: 12

    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property var socials: [
        { id: "bilibili", name: qsTr("哔哩哔哩") },
        { id: "douyin", name: qsTr("抖音") },
        { id: "x", name: "X" },
        { id: "github", name: "GitHub" },
        { id: "youtube", name: "YouTube" }
    ]

    component SectionTitle: ColumnLayout {
        id: section
        property alias text: title.text
        property alias hint: hintText.text
        // A block inside a section (中国大陆用户) rather than a section of its own.
        property bool small: false
        property real motionDy: 0
        transform: Translate { y: motionDy }
        Layout.fillWidth: true
        Layout.topMargin: small ? 6 : 10
        spacing: 3
        VH3 { id: title; font.pixelSize: section.small ? 13.5 : Theme.fsH3 }
        Text {
            id: hintText
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            wrapMode: Text.WordWrap
        }
    }

    VH1 { text: qsTr("赞助作者") }

    // The appeal, on the accent-tinted plate the home page uses for 继续上次.
    Rectangle {
        id: hero
        property real motionDy: 0
        transform: Translate { y: hero.motionDy }
        Layout.fillWidth: true
        implicitHeight: heroRow.implicitHeight + 36
        radius: Theme.rCard
        border.width: 1
        border.color: Theme.stroke
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.accentSoft }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.03) }
        }
        RowLayout {
            id: heroRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 16
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 48; implicitHeight: 48; radius: 14
                color: Theme.accentSoft
                VIcon { anchors.centerIn: parent; name: "coffee"; size: 24; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Veyra 开源免费，也需要你的支持")
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("开发和测试设备的开销一直由作者自己承担。如果 Veyra 对你有帮助，欢迎请作者喝杯咖啡，你的支持就是持续更新的动力。")
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    lineHeight: 1.25
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    SectionTitle { text: qsTr("关注作者") }

    // One tile per platform: the brand tile, its name and 前往主页. The whole card
    // is the button; it lifts on hover like the home page's source cards.
    Flow {
        id: socialFlow
        property real motionDy: 0
        transform: Translate { y: socialFlow.motionDy }
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: panel.socials
            delegate: Item {
                id: slot
                required property var modelData
                width: 128
                height: 138
                Rectangle {
                    id: social
                    objectName: "sponsor-link-" + slot.modelData.id
                    width: parent.width
                    height: parent.height
                    radius: Theme.rCard
                    color: socialHover.hovered ? Theme.card3 : Theme.card2
                    border.width: 1
                    border.color: socialHover.hovered ? Theme.stroke2 : Theme.stroke
                    y: socialHover.hovered ? -3 : 0
                    Behavior on y { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
                    scale: socialTap.pressed ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 0
                        Image {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            source: "sponsor/" + slot.modelData.id + ".svg"
                            sourceSize.width: Math.ceil(44 * panel.dpr)
                            sourceSize.height: Math.ceil(44 * panel.dpr)
                            smooth: true
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 9
                            text: slot.modelData.name
                            color: Theme.t1
                            font.family: Theme.fontUi
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 10
                            implicitWidth: goRow.implicitWidth + 20
                            implicitHeight: 26
                            radius: 8
                            color: socialHover.hovered ? Theme.accent : Qt.rgba(1, 1, 1, 0.06)
                            Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
                            Row {
                                id: goRow
                                anchors.centerIn: parent
                                spacing: 5
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("前往主页")
                                    color: socialHover.hovered ? Theme.accentInk : Theme.t1
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSmall
                                    font.weight: Font.Medium
                                }
                                VIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "external"
                                    size: 12
                                    color: socialHover.hovered ? Theme.accentInk : Theme.t2
                                }
                            }
                        }
                    }
                    HoverHandler { id: socialHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { id: socialTap; onTapped: panel.openLink(slot.modelData.id) }
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("请作者喝杯咖啡")
        hint: qsTr("金额随意，每一份支持都是很大的鼓励")
    }

    // Ko-fi first, for everyone: drawn as its official "Buy me a coffee" button (the
    // one in the README).
    Flow {
        id: kofiRow
        property real motionDy: 0
        transform: Translate { y: kofiRow.motionDy }
        Layout.fillWidth: true
        spacing: 14
        Rectangle {
            id: kofi
            objectName: "sponsor-link-kofi"
            width: kofiContent.implicitWidth + 40
            height: 46
            radius: height / 2
            color: kofiHover.hovered ? "#83AFF5" : "#72A4F2"
            Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
            scale: kofiTap.pressed ? 0.96 : 1.0
            Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
            Row {
                id: kofiContent
                anchors.centerIn: parent
                spacing: 9
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; height: 34
                    source: "sponsor/kofi.svg"
                    sourceSize.width: Math.ceil(34 * panel.dpr)
                    sourceSize.height: Math.ceil(34 * panel.dpr)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Buy me a coffee"
                    color: "#FFFFFF"
                    font.family: Theme.fontUi
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
            }
            HoverHandler { id: kofiHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: kofiTap; onTapped: panel.openLink("kofi") }
        }
    }

    SectionTitle {
        objectName: "sponsor-mainland"
        small: true
        text: qsTr("中国大陆用户")
        hint: qsTr("可以用微信或支付宝扫码")
    }

    // The two codes side by side at one size; a narrow window stacks them.
    Flow {
        id: codes
        property real motionDy: 0
        transform: Translate { y: codes.motionDy }
        Layout.fillWidth: true
        spacing: 14
        Repeater {
            model: [
                { id: "wechat", label: qsTr("微信扫码") },
                { id: "alipay", label: qsTr("支付宝扫码") }
            ]
            delegate: Rectangle {
                id: code
                required property var modelData
                objectName: "sponsor-code-" + modelData.id
                width: 244
                // Both cards take the taller code's height (572x602) so the labels line up.
                height: 12 + 232 + 46
                radius: 16
                color: Theme.card2
                border.width: 1
                border.color: Theme.stroke
                Image {
                    id: qr
                    objectName: "sponsor-qr-" + code.modelData.id
                    x: 12; y: 12
                    width: 220
                    height: 232
                    source: "sponsor/" + code.modelData.id + ".png"
                    // Decoded at the shown size: the reader's smooth scaling keeps the
                    // modules crisp where a GPU-minified 572 px image would alias.
                    sourceSize.width: Math.ceil(220 * panel.dpr)
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignTop
                    smooth: true
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    spacing: 7
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18
                        source: "sponsor/" + code.modelData.id + ".svg"
                        sourceSize.width: Math.ceil(18 * panel.dpr)
                        sourceSize.height: Math.ceil(18 * panel.dpr)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: code.modelData.label
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    Text {
        id: thanks
        property real motionDy: 0
        transform: Translate { y: thanks.motionDy }
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: qsTr("赞助完全自愿，不影响任何功能。谢谢你的支持！")
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.WordWrap
    }
}
