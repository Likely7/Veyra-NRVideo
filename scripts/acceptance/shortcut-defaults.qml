// Loaded only into a private staging copy, never the user's running package.
// claude-capture-fps-audio-20261027: the screenshot / record shortcuts on the
// settings page, with the defaults or with a key an older profile already took.
import QtQuick
Item {
    id: test
    property var expect: ({})
    Timer {
        interval: 1500; running: Object.keys(test.expect).length > 0
        onTriggered: {
            const keys = veyra.shortcuts
            let ok = true
            for (const id in test.expect) ok = ok && keys[id] === test.expect[id]
            // The settings page shows every row, an unbound one as 未设置.
            const rows = []
            const walk = item => { if (item.objectName && item.objectName.startsWith("key-")) rows.push(item.objectName); for (const c of item.children) walk(c) }
            walk(test.parent.parent)   // the Loader's parent: the window's content item
            ok = ok && rows.indexOf("key-screenshot") >= 0 && rows.indexOf("key-record") >= 0
            veyra.logUi("shortcut-test", "keys=" + JSON.stringify(keys) + " rows=" + rows.join(","))
            console.log(ok ? "SC_PASS" : "SC_FAIL " + JSON.stringify(keys) + " " + rows.join(","))
            Qt.quit()
        }
    }
}
