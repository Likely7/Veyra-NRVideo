import QtQuick

// An accent fill: the flat accent, or with the dusk palette's sunset accent
// (Theme.accentGradient) a peach -> pink gradient; farEnd carries it on to lilac
// (the seek bars). span / spanX: the stretch the gradient runs over and this
// item's place in it, so a bar keeps its colours where they are while it fills
// instead of squeezing them into the filled part.
Rectangle {
    id: fill
    property bool farEnd: false
    property real span: width
    property real spanX: 0
    readonly property bool graded: Theme.accentGradient
    color: graded ? "transparent" : Theme.accent
    clip: graded && (span > width + 0.5 || spanX > 0.5)
    Rectangle {
        visible: fill.graded
        x: -fill.spanX
        width: Math.max(fill.width, fill.span)
        height: fill.height
        radius: fill.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.accentFrom }
            GradientStop { position: fill.farEnd ? 0.62 : 1.0; color: Theme.accentTo }
            GradientStop { position: 1.0; color: fill.farEnd ? Theme.accentFar : Theme.accentTo }
        }
    }
}
