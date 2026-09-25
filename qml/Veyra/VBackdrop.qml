import QtQuick
import QtQuick.Shapes

// The window background, .vy in app.css: an elliptical radial gradient under a fine
// grain. backdrop.png is that gradient computed in float and dithered once
// (tools/qt_probe/make-backdrop.py). A QML RadialGradient was tried first: the shape
// matched but on a 7..28 level ramp it quantised into visible rings (G1.2 evidence).
// The CSS gradient is relative to the box, so stretching the image keeps its shape.
// The Shape only gives it the window's rounded corners and the 1px stroke.
Shape {
    id: backdrop
    property int radius: Theme.rWindow
    property color strokeColor: Theme.stroke
    // fillItem crashes the CurveRenderer in Qt 6.8.3 (access violation, reproduced
    // with qml.exe); the geometry renderer is fine, with multisampling for the corners.
    preferredRendererType: Shape.GeometryRenderer
    layer.enabled: true
    layer.samples: 4

    Image {
        id: fill
        visible: false
        width: backdrop.width
        height: backdrop.height
        source: "backdrop.png"
        smooth: true
    }

    ShapePath {
        strokeWidth: 1
        strokeColor: backdrop.strokeColor
        fillItem: fill
        PathRectangle {
            x: 0.5; y: 0.5
            width: backdrop.width - 1; height: backdrop.height - 1
            radius: backdrop.radius
        }
    }
}
