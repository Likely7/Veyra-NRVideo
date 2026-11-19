import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 600
    height: 480

    ScreenCropEditor {
        id: editor
        hostRect: Qt.rect(30, 30, 500, 260)
    }

    TestCase {
        name: "ScreenCrop"
        when: windowShown

        function test_drag_resize_move_and_apply() {
            editor.begin({left:0, top:0, right:0, bottom:0})
            compare(editor.scaleFactor, 0.5)
            const dragArea = findChild(editor, "screen-crop-drag")
            verify(dragArea && dragArea.visible)
            mouseDrag(dragArea, 2, 2, 24, 12)
            compare(editor.cut.left, 48)
            compare(editor.cut.top, 24)
            mouseDrag(dragArea, dragArea.width - 2, dragArea.height - 2, -16, -8)
            compare(editor.cut.right, 32)
            compare(editor.cut.bottom, 16)
            mouseDrag(dragArea, 200, 100, 5, -5)
            compare(editor.cut.left, 58)
            compare(editor.cut.top, 14)
            compare(editor.cut.right, 22)
            compare(editor.cut.bottom, 26)
            const applyButton = findChild(editor, "screen-crop-apply")
            verify(applyButton && applyButton.enabled)
            mouseClick(applyButton)
            compare(editor.editing, false)
            compare(veyra.appliedCrop.left, 58)
            compare(veyra.appliedCrop.bottom, 26)
        }
    }
}
