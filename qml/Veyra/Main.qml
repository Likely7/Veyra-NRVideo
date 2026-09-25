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

    // The one place the native video window sits. There must be exactly ONE item
    // named videoHost in the tree: main.cpp finds it by name, and an earlier
    // version had a copy in each page, so the engine bound its swapchain to a
    // hidden page's zero-sized item and presented into a 1x1 window.
    //
    // The pages do not own the video; they declare where it should go (an item
    // named videoArea) and this follows the active page.
    Item {
        id: videoHost
        objectName: "videoHost"
        // Visible on every video page, not only once a source is open. It has to
        // be visible and correctly sized BEFORE the engine creates its swapchain:
        // the swapchain is built from this window's client size, and gating it on
        // hasSource meant the engine built it from a hidden 0x0 window and
        // presented into a 1x1 surface.
        visible: root.pageIsVideo
        z: 1
    }

    readonly property bool pageIsVideo: page === "minimal" || page === "pro" || page === "node"

    // Finds the video area of the page that is actually showing. Deliberately
    // scoped to the current page: reading every page's area is what caused the
    // 1x1 bug in the first place.
    //
    // The position is computed here rather than read from a child item's
    // geometry, because the page does not lay itself out until the frame after
    // the switch. On the frame the user opens a file, every child item still
    // reports 0x0, and the presenter then builds a 1x1 swapchain. So the host's
    // rect is derived from the same expressions the pages use, which are known
    // synchronously.
    function updateVideoHost() {
        // Computed from `page` directly, NOT from the pageIsVideo property: this
        // runs from onPageChanged, and a binding on `page` has not re-evaluated
        // at that point. Reading the property here returned the PREVIOUS page's
        // answer, so switching to a video page left the host at 0x0 and the
        // presenter built a 1x1 swapchain.
        const isVideo = (page === "minimal" || page === "pro" || page === "node")
        if (!isVideo) {
            videoHost.width = 0
            videoHost.height = 0
            return
        }
        if (page === "minimal") {
            videoHost.x = 0
            videoHost.y = 0
            videoHost.width = root.width
            videoHost.height = root.height
        } else if (page === "pro") {
            // Pro: full height on the left, the effect panel is a fixed 360 wide.
            videoHost.x = 0
            videoHost.y = 0
            videoHost.width = Math.max(1, root.width - 360)
            videoHost.height = Math.max(1, root.height - 44)
        } else {
            // Node: video on top, 40px toolbar between it and the canvas below.
            videoHost.x = 0
            videoHost.y = 0
            videoHost.width = Math.max(1, root.width)
            videoHost.height = Math.max(1, Math.round(Math.max(220, root.height * 0.42)))
        }
    }

    onPageChanged: updateVideoHost()
    onWidthChanged: updateVideoHost()
    onHeightChanged: updateVideoHost()

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
    // and the engine never disagree about where the user is. The host rect is
    // settled here as well: it must be correct before the first open, not one
    // frame later.
    Component.onCompleted: {
        if (!veyra.hasSource) root.page = "home"
        updateVideoHost()
    }
}
