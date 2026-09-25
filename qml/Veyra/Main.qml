// The application shell.
//
// The window is frameless with a small radius (Windows' own default is small),
// because the previous build's large rounding was rejected. The video area is
// its own item and is NEVER clipped or rounded: rounding crops the picture.
//
// Layout of the shell, top to bottom:
//   * the top dock, hidden until the pointer reaches the top edge;
//   * the page area, which swaps between home / minimal / pro / node / export /
//     settings;
//   * nothing else. The video is a native window placed over `videoHost` by
//     apps/veyra-qml/main.cpp, so it is not part of this scene graph.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 1280
    height: 800
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "Veyra " + veyra.version

    // The current page. Changing it is what the dock and the engine's
    // `navigate` signal both drive.
    property string page: "home"

    // The window background: deep black with a slight gradient, replacing the
    // old translucent glass. This has to be an item, not Window.color: a
    // frameless window cannot carry a gradient, and without this the desktop
    // shows straight through the app.
    Rectangle {
        id: backdrop
        anchors.fill: parent
        radius: Theme.radiusWindow
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundTop }
            GradientStop { position: 0.45; color: Theme.backgroundMid }
            GradientStop { position: 1.0; color: Theme.background }
        }
        // A hairline edge, so the small radius reads against a dark desktop.
        border.width: 1
        border.color: Theme.stroke2
    }

    // Every page is instantiated once and kept alive: switching pages must not
    // rebuild the chain editor or lose a half-typed preset name.
    StackLayout {
        id: pages
        anchors.fill: parent
        currentIndex: {
            switch (root.page) {
            case "minimal": return 1
            case "pro": return 2
            case "node": return 3
            case "export": return 4
            case "settings": return 5
            default: return 0
            }
        }

        HomePage { onRequestPage: page => root.page = page }
        MinimalPage { onRequestPage: page => root.page = page }
        ProPage { onRequestPage: page => root.page = page }
        NodePage { onRequestPage: page => root.page = page }
        ExportPage { onRequestPage: page => root.page = page }
        SettingsPage { onRequestPage: page => root.page = page }
    }

    // The dock floats above the pages and hides itself again on a timer.
    TopDock {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        currentPage: root.page
        onRequestPage: page => root.page = page
    }

    // Transient notices from the engine: a refused chain edit, a finished
    // export, a failed open. Shown once and gone; no dialog to dismiss.
    Rectangle {
        id: toast
        property string text: ""
        property bool isError: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: Math.min(toastText.implicitWidth + 32, root.width - 64)
        height: tostRow.implicitHeight + 20
        radius: Theme.radiusControl
        color: toast.isError ? "#2A1416" : Theme.card2
        border.width: 1
        border.color: toast.isError ? Theme.err : Theme.stroke2
        opacity: 0
        visible: opacity > 0.01
        // A spring pop-in, an eased fade-out: arriving should feel alive, leaving
        // should not linger over the picture.
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationNormal; easing.bezierCurve: Theme.easeOut }
        }
        function show(message, error) {
            toast.text = message
            toast.isError = error
            toast.opacity = 1
            hideTimer.restart()
        }
        Timer { id: hideTimer; interval: 4200; onTriggered: toast.opacity = 0 }
        RowLayout {
            id: tostRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                color: toast.isError ? Theme.err : Theme.ok
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                id: toastText
                text: toast.text
                color: Theme.text1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.NoWrap
            }
        }
    }

    Connections {
        target: veyra
        function onNotice(text, isError) { toast.show(text, isError) }
        function onNavigate(page) { root.page = page }
    }

    // The bridge drives page changes from engine-side commands too, so the dock
    // and the engine never disagree about where the user is.
    Component.onCompleted: {
        if (!veyra.hasSource) root.page = "home"
    }
}
