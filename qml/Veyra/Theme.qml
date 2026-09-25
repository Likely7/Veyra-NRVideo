// Design tokens, transcribed from the approved prototype's app.css.
//
// Every value here is copied from the prototype rather than chosen. That is the
// point of this file: the interface has to match the approved design, so the
// numbers live in one place that can be diffed against app.css.
pragma Singleton
import QtQuick

QtObject {
    // --- surfaces (app.css .vy) -------------------------------------------
    readonly property color card: "#131317"
    readonly property color card2: "#19191E"
    readonly property color card3: "#24242A"
    readonly property color popover: "#1B1B20"
    readonly property color dialog: "#141418"
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.07)
    readonly property color stroke2: Qt.rgba(1, 1, 1, 0.13)
    // The app background: a radial gradient in the prototype, approximated here
    // with the three stops it interpolates between.
    readonly property color bgOuter: "#070709"
    readonly property color bgMid: "#0C0C10"
    readonly property color bgInner: "#17171C"
    readonly property color videoBlack: "#000000"

    // --- text -------------------------------------------------------------
    readonly property color t1: "#F3F3F5"
    readonly property color t2: Qt.rgba(0.953, 0.953, 0.961, 0.62)
    readonly property color t3: Qt.rgba(0.953, 0.953, 0.961, 0.38)

    // --- accent and state -------------------------------------------------
    readonly property color accent: "#FF8A3D"
    readonly property color accentInk: "#1B0E04"
    readonly property color accentSoft: Qt.rgba(1, 0.541, 0.239, 0.14)
    readonly property color accentGlow: Qt.rgba(1, 0.541, 0.239, 0.35)
    readonly property color ok: "#3DDC84"
    readonly property color okGlow: Qt.rgba(0.239, 0.863, 0.518, 0.16)
    readonly property color warn: "#F5C84B"
    readonly property color warnGlow: Qt.rgba(0.961, 0.784, 0.294, 0.16)
    readonly property color err: "#FF5D5D"
    readonly property color errGlow: Qt.rgba(1, 0.365, 0.365, 0.16)
    readonly property color exp: "#B79BFF"
    readonly property color expSoft: Qt.rgba(0.718, 0.608, 1.0, 0.14)

    // --- geometry (--r-win / --r-card / --r-ctl) --------------------------
    // The window radius is 8px, small, because Windows' own default is small.
    // The video surface is NEVER rounded: .video in the prototype carries
    // border-radius:0!important because rounding crops the picture.
    readonly property int rWindow: 8
    readonly property int rCard: 14
    readonly property int rCtl: 10

    // --- motion (--spring / --spring-soft / --out) ------------------------
    // Four control points exactly: QEasingCurve.BezierSpline rejects any other
    // count, and a rejected curve silently falls back to linear.
    readonly property int durFast: 150
    readonly property int durNormal: 240
    readonly property int durSlow: 450
    readonly property var spring: [0.34, 1.56, 0.64, 1.0]
    readonly property var springSoft: [0.22, 1.25, 0.36, 1.0]
    readonly property var easeOut: [0.2, 0.8, 0.2, 1.0]

    // --- type (--f-ui / --f-mono and the .h1/.h2/.h3 scale) ---------------
    readonly property string fontUi: "Microsoft YaHei UI"
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
