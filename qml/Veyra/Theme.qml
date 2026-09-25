// Design tokens, ported from the approved browser prototype
// (prototypes/ui-redesign-2026-09-25/app.css) so the QML build and the
// prototype that was signed off cannot drift apart.
//
// Single dark look on purpose: the user asked for deep black with a slight
// gradient instead of the old translucent glass, and there is no light theme.
pragma Singleton
import QtQuick

QtObject {
    // --- surfaces ---------------------------------------------------------
    readonly property color background: "#070709"
    readonly property color backgroundTop: "#17171C"
    readonly property color backgroundMid: "#0C0C10"
    readonly property color card: "#131317"
    readonly property color card2: "#19191E"
    readonly property color card3: "#24242A"
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.07)
    readonly property color stroke2: Qt.rgba(1, 1, 1, 0.13)

    // --- text -------------------------------------------------------------
    readonly property color text1: "#F3F3F5"
    readonly property color text2: Qt.rgba(0.953, 0.953, 0.961, 0.62)
    readonly property color text3: Qt.rgba(0.953, 0.953, 0.961, 0.38)

    // --- accent and state -------------------------------------------------
    readonly property color accent: "#FF8A3D"
    readonly property color accentInk: "#1B0E04"
    readonly property color accentSoft: Qt.rgba(1, 0.541, 0.239, 0.14)
    readonly property color accentGlow: Qt.rgba(1, 0.541, 0.239, 0.35)
    readonly property color ok: "#3DDC84"
    readonly property color warn: "#F5C84B"
    readonly property color err: "#FF5D5D"
    readonly property color experimental: "#B79BFF"

    // --- geometry ---------------------------------------------------------
    // The window radius is small because Windows' own default is small; the
    // video itself is NEVER rounded (rounding crops the picture), which is why
    // there is no radius token used anywhere near the video host.
    readonly property int radiusWindow: 8
    readonly property int radiusCard: 14
    readonly property int radiusControl: 10

    // --- motion -----------------------------------------------------------
    // Elastic by request. The durations are short: this is a player, and an
    // interface that animates for half a second gets in the way of playback.
    readonly property int durationFast: 120
    readonly property int durationNormal: 220
    readonly property int durationSlow: 380
    // Overshooting curves, the QML equivalent of the prototype's
    // cubic-bezier(.34,1.56,.64,1). QEasingCurve.BezierSpline takes exactly four
    // control points; passing six made every animation log "Invalid bezier curve"
    // and fall back to linear, which is why the interface did not spring.
    readonly property var spring: [0.34, 1.56, 0.64, 1.0]
    readonly property var springSoft: [0.22, 1.25, 0.36, 1.0]
    readonly property var easeOut: [0.2, 0.8, 0.2, 1.0]

    // --- type -------------------------------------------------------------
    readonly property string fontUi: "Microsoft YaHei UI"
    readonly property string fontMono: "Consolas"
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 16
    readonly property int fontSizeHeading: 22

    readonly property int spacing: 8
    readonly property int gutter: 16
}
