import QtQuick
import QtTest
import Veyra

Item {
    width: 400; height: 240
    VBackdrop { id: backdrop; anchors.fill: parent; softwareRendering: true }
    TestCase {
        name: "SoftwareBackdrop"
        when: windowShown
        function test_opaque_background_data() { return [{tag:"flat",level:0},{tag:"gradient",level:1},{tag:"strong",level:2}] }
        function test_opaque_background(data) {
            Theme.backdropLevel=data.level
            waitForRendering(backdrop)
            const shot=grabImage(backdrop)
            compare(shot.alpha(200,120),255)
            verify(shot.red(200,120)<80 && shot.green(200,120)<80 && shot.blue(200,120)<80)
        }
    }
}
