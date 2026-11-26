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
    readonly property bool graded: false
    color: Theme.accent
}
