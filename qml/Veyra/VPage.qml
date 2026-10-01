// A page of the shell (app.css .page): shown while current or while sinking out.
// Main.qml owns the switch (core.js app.go); this only reads its state. The sink
// is @keyframes sink { to { opacity: 0; transform: scale(.985) } } over .22s --out,
// and the sinking page takes no input (.page.leave { pointer-events: none }).
// enter() is emitted when the page shows through an animated switch; the VRise
// items declared directly in the page play on it.
import QtQuick

Item {
    id: page
    property string pageId
    signal enter()
    // The item the page sits in (Main.qml's page stack). A page that is neither shown nor
    // sinking out leaves the scene: its live bindings kept changing while hidden and Qt
    // redrew the whole window for each change (~11 times a second during capture), which
    // cost GPU time and kept OBS game capture on the UI instead of the video.
    property Item home: null
    parent: (current || leaving) ? home : null
    readonly property var shell: home ? home.Window.window : null
    readonly property bool current: shell !== null && shell.shownPage === pageId
    readonly property bool leaving: shell !== null && shell.leavingPage === pageId
    width: home ? home.width : 0
    height: home ? home.height : 0
    visible: current || leaving
    enabled: current
    opacity: leaving ? 1 - shell.leaveT : 1
    scale: leaving ? 1 - 0.015 * shell.leaveT : 1
}
