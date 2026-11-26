// Design tokens, transcribed from the approved prototype's app.css.
//
// Every classic value here is copied from the prototype rather than chosen. That
// is the point of this file: the interface has to match the approved design, so
// the numbers live in one place that can be diffed against app.css.
//
// 设置 → 外观 → 界面配色 adds a second palette, 日落 (dusk, the default since
// 2026-11-14): the same depths tinted indigo-violet, a sunset accent that fills
// switches, sliders, primary buttons and seek bars as a peach -> pink gradient
// (VAccentFill.qml), and a twilight backdrop (tools/qt_probe/make-backdrop-dusk.py).
// Its values are our own, chosen after sunset skies. "classic" keeps every value
// of the prototype exactly.
pragma Singleton
import QtQuick

QtObject {
    // --- palette ------------------------------------------------------------
    property string paletteName: "dusk"
    readonly property bool dusk: paletteName !== "classic"

    // --- surfaces (Studio Cobalt graphite ramp) ----------------------------
    readonly property color bg: "#0B0C0E"
    readonly property color card: "#121418"
    readonly property color card2: "#181B21"
    readonly property color card3: "#21252D"
    readonly property color popover: "#181B21"
    readonly property color dialog: "#121418"
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.08)
    readonly property color strokeStrong: Qt.rgba(1, 1, 1, 0.14)
    readonly property color stroke2: strokeStrong
    readonly property color bgOuter: bg
    readonly property color bgMid: card
    readonly property color bgInner: card2
    readonly property color videoBlack: "#000000"
    function shade(r, g, b, a) {
        const alpha = a === undefined ? 1 : a
        return Qt.rgba(r / 255, g / 255, b / 255, alpha)
    }

    // --- text -------------------------------------------------------------
    readonly property color t1: "#F4F5F7"
    readonly property color t2: "#A6ADBB"
    readonly property color t3: "#7E8696"

    // --- accent and state (Studio Cobalt) ---------------------------------
    property string accentName: "orange"
    readonly property bool accentWhite: accentName === "white"
    readonly property color accent: accentWhite ? "#F4F5F7" : "#2563EB"
    readonly property color accent2: accent
    readonly property color accentHover: accentWhite ? "#FFFFFF" : "#3B82F6"
    readonly property color accentPressed: accentWhite ? "#D4D7DD" : "#1D4ED8"
    readonly property color accentText: accentWhite ? "#0B0C0E" : "#FFFFFF"
    readonly property color accentInk: accentText
    readonly property color focusRing: "#60A5FA"
    readonly property color accentSoft: accentWhite ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(37 / 255, 99 / 255, 235 / 255, 0.16)
    readonly property color accentGlow: Qt.rgba(37 / 255, 99 / 255, 235 / 255, 0.16)
    readonly property bool accentGradient: false
    readonly property color accentFrom: accent
    readonly property color accentTo: accent
    readonly property color accentFar: accent
    // 设置 → 背景渐变强度: 0 none, 1 light, 2 medium (flat matte graphite by default).
    property int backdropLevel: 0
    readonly property string backdropImage: (dusk ? "backdrop-dusk" : "backdrop") + (backdropLevel === 2 ? "-strong" : "") + ".png"
    readonly property string logoImage: dusk ? "logo-dusk.png" : "logo.png"
    readonly property color ok: "#34D399"
    readonly property color okGlow: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.16)
    readonly property color warn: "#FBBF24"
    readonly property color warnGlow: Qt.rgba(251 / 255, 191 / 255, 36 / 255, 0.16)
    readonly property color err: "#F87171"
    readonly property color errGlow: Qt.rgba(248 / 255, 113 / 255, 113 / 255, 0.16)
    readonly property color exp: "#B79BFF"
    readonly property color expSoft: Qt.rgba(0.718, 0.608, 1.0, 0.14)

    // --- interaction tokens -----------------------------------------------
    readonly property color hoverFill: Qt.rgba(1, 1, 1, 0.06)
    readonly property color pressFill: Qt.rgba(1, 1, 1, 0.10)
    readonly property real disabledOpacity: 0.45

    // --- spacing scale ----------------------------------------------------
    readonly property int s4: 4
    readonly property int s8: 8
    readonly property int s12: 12
    readonly property int s16: 16
    readonly property int s20: 20
    readonly property int s24: 24
    readonly property int s32: 32

    // --- geometry (--r-win / --r-card / --r-ctl) --------------------------
    readonly property int rWindow: 8
    readonly property int rCard: 10
    readonly property int rCtl: 6
    readonly property int rModal: 12

    // --- motion (--spring / --spring-soft / --out) ------------------------
    // easing.bezierCurve takes whole cubic segments, six numbers each
    // [c1x, c1y, c2x, c2y, endX, endY], the last one ending at (1, 1). Any other
    // length is rejected and the animation silently runs linear (G0.5/G0.6 caught
    // the old four-number curves doing exactly that).
    // --spring and --spring-soft are CSS linear() functions: straight lines between
    // stops. A cubic with its control points at 1/3 and 2/3 of a line is that line,
    // so each stop becomes one segment and the curve matches the design exactly,
    // overshoot included. Stops without a position are spread evenly, as in CSS.
    // At most 10 segments: Qt 6.8.3 corrupts the heap when a BezierSpline curve of
    // 11+ segments is destroyed (reproduced in C++ with no QML, G1.1). --spring has
    // 12, so its 1.035@28.4% and .998@38.5% stops are dropped; the remaining line
    // stays within 0.007 of the design curve (veyra_qml_easing_tests checks 0.01).
    // Reduced motion (the design's .vy.reduced: every transition and animation at
    // 0s). Main.qml binds it to the saved setting or the --reduced-motion switch;
    // every duration in the app goes through d(), so one flag covers all of them.
    // Endless animations also stop on it: a zero-length loop would spin.
    property bool reduced: false
    function d(ms) { return reduced ? 0 : ms }

    readonly property int animFast: 150
    readonly property int animNormal: 200
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
    // linear(0,.009,.035 2.1%,.141 4.4%,.723 12.9%,.938 16.7%,1.017 20.2%,1.043 24%,
    //        1.035 28.4%,.998 38.5%,.99 44.1%,1.001 60.7%,1)
    readonly property var spring: linearEasing([
        [0, 0], [0.0105, 0.009], [0.021, 0.035], [0.044, 0.141], [0.129, 0.723],
        [0.167, 0.938], [0.202, 1.017], [0.24, 1.043],
        [0.441, 0.99], [0.607, 1.001], [1, 1]])
    // linear(0,.02,.08 3.2%,.3 7.5%,.84 17%,1.01 25%,1.025 31%,1.004 45%,1)
    readonly property var springSoft: linearEasing([
        [0, 0], [0.016, 0.02], [0.032, 0.08], [0.075, 0.3], [0.17, 0.84],
        [0.25, 1.01], [0.31, 1.025], [0.45, 1.004], [1, 1]])
    // cubic-bezier(.2,.8,.2,1)
    readonly property var easeOut: [0.2, 0.8, 0.2, 1.0, 1.0, 1.0]

    // --- type (--f-ui / --f-mono and the .h1/.h2/.h3 scale) ---------------
    // --f-ui is "Geist", "Noto Sans SC", ...: Geist is not on this machine (D4), so
    // the design's own second choice, which is also what Edge rendered the reference
    // with. Noto Sans SC ships as a variable font; Qt 6.8.3 maps font.weight 600 to a
    // thin instance of it (500 and 700 are fine), so every DemiBold also sets the wght
    // axis directly (G1.3, tmp/.../min/Font.qml).
    // Per interface language (Qt.uiLanguage, set by the bridge): Japanese kana and kanji
    // and Traditional characters read best in their own Windows UI fonts.
    // Korean uses Windows' Hangul UI font instead of Simplified Chinese glyph fallback.
    readonly property string fontUi: Qt.uiLanguage === "ja" ? "Yu Gothic UI"
                                   : Qt.uiLanguage === "ko" ? "Malgun Gothic"
                                   : Qt.uiLanguage === "zh-TW" ? "Microsoft JhengHei UI" : "Noto Sans SC"
    readonly property var axesDemiBold: ({ "wght": 600 })
    // 500: the design's .seek .peek time strip is `font: 500 11px/1 var(--f-mono)`.
    // Same reason as DemiBold - 600 lands on a thin instance, so the axis is set
    // explicitly rather than left to font.weight.
    readonly property var axesMedium: ({ "wght": 500 })
    readonly property string fontMono: "Consolas"
    readonly property real fsEyebrow: 11
    readonly property real fsSmall: 11.5
    readonly property real fsBody: 12.5
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
