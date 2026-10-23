import QtQuick
import QtTest
import Veyra

Item {
    width: 400; height: 200
    QtObject {
        id: veyra
        property bool hasSource: true
        property int previewRotation: 0
        function rotatePictureClockwise() { previewRotation = (previewRotation + 90) % 360 }
    }
    RotateButton { id: pro; x: 50; y: 60 }
    RotateButton { id: cine; x: 150; y: 60 }
    TestCase {
        name: "PictureRotation"
        when: windowShown
        function init() { veyra.hasSource = true; veyra.previewRotation = 0 }
        function test_sharedAndWrap() {
            for (let n = 1; n <= 12; ++n) {
                const button = n % 2 ? pro : cine
                mouseClick(button, button.width / 2, button.height / 2)
                compare(veyra.previewRotation, (n % 4) * 90)
                verify(pro.tip.indexOf(String(veyra.previewRotation) + "°") >= 0)
                compare(pro.tip, cine.tip)
            }
        }
        function test_noSource() {
            veyra.hasSource = false
            mouseClick(pro, 14, 14)
            mouseClick(cine, 14, 14)
            compare(veyra.previewRotation, 0)
            compare(pro.enabled, false)
        }
    }
}
