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
    readonly property var shell: Window.window
    readonly property bool current: shell !== null && shell.shownPage === pageId
    readonly property bool leaving: shell !== null && shell.leavingPage === pageId
    anchors.fill: parent
    visible: current || leaving
    enabled: current
    opacity: leaving ? 1 - shell.leaveT : 1
    scale: leaving ? 1 - 0.015 * shell.leaveT : 1
}
