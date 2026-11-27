// Desktop workspace tokens: flat surfaces, square panels, restrained accent.
pragma Singleton
import QtQuick

QtObject {
    // --- palette ------------------------------------------------------------
    property string paletteName: "dusk"
    readonly property bool dusk: paletteName !== "classic"

    // --- surfaces --------------------------------------------------------
    readonly property color card: dusk ? "#202226" : "#1C1D20"
    readonly property color card2: dusk ? "#25272B" : "#222326"
    readonly property color card3: dusk ? "#34373D" : "#303236"
    readonly property color popover: "#282A2E"
    readonly property color dialog: "#202226"
    readonly property color stroke: "#34373D"
    readonly property color stroke2: "#4A4E55"
    // One neutral background; legacy tokens remain available to callers.
    readonly property color bgOuter: dusk ? "#191B1F" : "#151619"
    readonly property color bgMid: bgOuter
    readonly property color bgInner: bgOuter
    readonly property color videoBlack: "#000000"
    // Keep legacy RGB callers neutral in both palettes.
    function shade(r, g, b, a) {
        const alpha = a === undefined ? 1 : a
        return Qt.rgba(r / 255, g / 255, b / 255, alpha)
    }

    // --- text -------------------------------------------------------------
    readonly property color t1: "#E8E9EB"
    readonly property color t2: "#BEC1C7"
    readonly property color t3: "#9398A1"

    // --- accent and state -------------------------------------------------
    // Preserve saved accent IDs; use a solid amber or neutral selection color.
    property string accentName: "orange"
    readonly property bool accentWhite: accentName === "white"
    signal accentPicked(string name)
    readonly property var accentOptions: [
        { id: "orange", color: "#D9A45B", label: qsTr("橙色") },
        { id: "blue", color: "#72A7F5", label: qsTr("蓝色") },
        { id: "green", color: "#80C695", label: qsTr("绿色") },
        { id: "teal", color: "#63C5BC", label: qsTr("Teal") },
        { id: "purple", color: "#B597EE", label: qsTr("紫色") },
        { id: "rose", color: "#DF96B0", label: qsTr("Rose") },
        { id: "red", color: "#EC8983", label: qsTr("红色") },
        { id: "white", color: "#E8E9EB", label: qsTr("白色") }
    ]
    function isAccent(name) { return accentOptions.some(option => option.id === name) }
    readonly property color accent: (accentOptions.find(option => option.id === accentName) || accentOptions[0]).color
    readonly property color accentInk: "#191B1F"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    readonly property color accentGlow: "transparent"

    // New Accent Colors
    readonly property color accentBlue: "#4B8DFF"
    readonly property color accentBlueInk: "#18316B"
    readonly property color accentBlueSoft: Qt.rgba(0.28, 0.53, 1.0, 0.1)

    readonly property color accentGreen: "#6AD85B"
    readonly property color accentGreenInk: "#183321"
    readonly property color accentGreenSoft: Qt.rgba(0.28, 0.53, 0.25, 0.1)

    readonly property color accentPurple: "#A365FF"
    readonly property color accentPurpleInk: "#4D227B"
    readonly property color accentPurpleSoft: Qt.rgba(0.67, 0.23, 1.0, 0.1)
    // Compatibility tokens for fills; all accent surfaces are now solid.
    readonly property bool accentGradient: false
    readonly property color accentFrom: accent
    readonly property color accentTo: accent
    readonly property color accentFar: accent
    // Retain stored background preferences without painting decorative gradients.
    property int backdropLevel: 1
    readonly property string backdropImage: (dusk ? "backdrop-dusk" : "backdrop") + (backdropLevel === 2 ? "-strong" : "") + ".png"
    // Monochrome branding in the app chrome.
    readonly property string logoImage: "logo.png"
    readonly property color ok: "#75B798"
    readonly property color okGlow: "transparent"
    readonly property color warn: "#F5C84B"
    readonly property color warnGlow: "transparent"
    readonly property color err: "#FF5D5D"
    readonly property color errGlow: "transparent"
    readonly property color exp: "#B79BFF"
    readonly property color expSoft: Qt.rgba(0.718, 0.608, 1.0, 0.14)

    // --- geometry (--r-win / --r-card / --r-ctl) --------------------------
    // The window radius is 8px, small, because Windows' own default is small.
    // The video surface is NEVER rounded: .video in the prototype carries
    // border-radius:0!important because rounding crops the picture.
    readonly property int rWindow: 8
    readonly property int rCard: 0
    readonly property int rCtl: 3
    readonly property int appBarHeight: 40

    // --- motion ----------------------------------------------------------
    // Respect the saved reduced-motion setting. Easing uses one bounded cubic
    // segment, keeping Qt's curve representation small and free of overshoot.
    property bool reduced: false
    function d(ms) { return reduced ? 0 : ms }

    readonly property int durFast: 150
    readonly property int durNormal: 240
    readonly property int durSlow: 450
    function linearEasing(stops) {
        var out = []
        for (var i = 1; i < stops.length; ++i) {
            var a = stops[i - 1], b = stops[i]
            out.push(a[0] + (b[0] - a[0]) / 3, a[1] + (b[1] - a[1]) / 3,
                     a[0] + (b[0] - a[0]) * 2 / 3, a[1] + (b[1] - a[1]) * 2 / 3,
                     b[0], b[1])
        }
        return out
    }
    // Keep the existing easing API, without overshoot.
    readonly property var easeOut: [0.2, 0.8, 0.2, 1.0, 1.0, 1.0]
    readonly property var spring: easeOut
    readonly property var springSoft: easeOut

    // Bundle Inter for consistent Latin/Greek/Cyrillic typography on every PC.
    // Keep native CJK fonts for scripts Inter does not cover.
    readonly property FontLoader uiTypeface: FontLoader {
        source: "fonts/InterVariable.ttf"
    }
    readonly property string fontUi: Qt.uiLanguage === "ja" ? "Yu Gothic UI"
                                   : Qt.uiLanguage === "ko" ? "Malgun Gothic"
                                   : Qt.uiLanguage === "zh-TW" ? "Microsoft JhengHei UI"
                                   : Qt.uiLanguage.indexOf("zh") === 0 ? "Noto Sans SC"
                                   : uiTypeface.status === FontLoader.Ready ? uiTypeface.name : "Segoe UI"
    readonly property var axesDemiBold: ({ "wght": 600 })
    // 500: the design's .seek .peek time strip is `font: 500 11px/1 var(--f-mono)`.
    // Same reason as DemiBold - 600 lands on a thin instance, so the axis is set
    // explicitly rather than left to font.weight.
    readonly property var axesMedium: ({ "wght": 500 })
    readonly property string fontMono: "Consolas"
    readonly property real fsEyebrow: 11
    readonly property real fsSmall: 12
    readonly property real fsBody: 13
    readonly property real fsH3: 15
    readonly property real fsH2: 18
    readonly property real fsH1: 26

    // --- component metrics from the CSS -----------------------------------
    readonly property int ctlHeight: 32      // .btn / .pill
    readonly property int rowMinHeight: 38   // .row
    readonly property int switchWidth: 34    // .sw
    readonly property int switchHeight: 20
    readonly property int switchKnob: 14
    readonly property int sliderKnob: 13
    readonly property int tagHeight: 20
    readonly property int dialogWidth: 640
    readonly property int popoverMinWidth: 220
}
