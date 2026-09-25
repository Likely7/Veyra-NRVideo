// A Lucide icon from IconData.js, drawn like the design's svg.i: 24x24 viewBox scaled
// to `size` (16 by default), no fill, stroke 1.7 in the current colour, round caps and
// joins. The stroke width is in viewBox units, as in the SVG, so it scales with size.
import QtQuick
import QtQuick.Shapes
import "IconData.js" as IconData

Item {
    id: icon
    property string name
    property color color: Theme.t1
    property real size: 16
    property real strokeWidth: 1.7
    // .play svg: fill:currentColor; stroke:none (the filled play/pause glyphs).
    property bool filled: false
    readonly property bool known: IconData.paths[name] !== undefined

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        visible: icon.known
        ShapePath {
            fillColor: icon.filled ? icon.color : "transparent"
            strokeColor: icon.filled ? "transparent" : icon.color
            strokeWidth: icon.strokeWidth
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.known ? IconData.paths[icon.name] : "" }
        }
    }
}
