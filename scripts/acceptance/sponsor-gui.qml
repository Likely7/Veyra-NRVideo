// Loaded into a private copy of Main.qml by sponsor-gui.py. Walks the home page's
// coffee line, 设置 → 赞助作者 and 关于 → 赞助作者 with real clicks: it prints
// "SPONSOR_UI_CLICK <name> <x> <y>" (window pixels) and the script posts the click to
// the window. Checks that nothing overflows its card in the current language and
// saves the home page, the settings page, the whole panel and each code card as images.
import QtQuick
import QtQuick.Window
Item {
    id: test
    property var appRoot     // Main's window
    property var settings    // the SettingsPage
    property string evidence: ""
    property int step: 0
    property int ticks: 0
    property int waitUntil: 0
    property int linkIndex: 0
    property bool saving: false
    readonly property var links: ["bilibili", "douyin", "x", "github", "youtube", "kofi"]
    function require(ok, message) {
        if (!ok) throw new Error(message)
        console.log("SPONSOR_UI_CHECK", message)
    }
    function window() { return test.Window.window.contentItem }
    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) { const r = find(item.children[i], name); if (r) return r }
        return null
    }
    function texts(item, out) {
        if (item instanceof Text && item.visible && item.text.length > 0) out.push(item)
        for (let i = 0; i < item.children.length; ++i) if (item.children[i].visible) texts(item.children[i], out)
        return out
    }
    // Every visible text of `box` lies inside it (2 px slack for antialiased edges).
    function fits(box, what) {
        for (const t of texts(box, [])) {
            const p = t.mapToItem(box, 0, 0)
            const w = Math.min(t.width, t.implicitWidth)
            test.require(p.x >= -2 && p.x + w <= box.width + 2, what + ": \"" + t.text + "\" fits (" + Math.round(p.x) + "+" + Math.round(w) + " in " + Math.round(box.width) + ")")
        }
    }
    function click(name, item, x, y) {
        const p = item.mapToItem(test.window(), x, y)
        const dpr = Screen.devicePixelRatio
        console.log("SPONSOR_UI_CLICK", name, Math.round(p.x * dpr), Math.round(p.y * dpr))
    }
    function shot(item, name, next) {
        test.saving = true
        item.grabToImage(r => { test.require(r.saveToFile(test.evidence + "/" + name + ".png"), "saved " + name); test.saving = false; test.step = next })
    }
    // Scroll the settings body so `item` is in view before it is clicked.
    function reveal(item) {
        let flick = item.parent
        while (flick && flick.contentY === undefined) flick = flick.parent
        if (!flick) return
        const y = item.mapToItem(flick.contentItem, 0, 0).y - 40
        flick.contentY = Math.max(0, Math.min(y, Math.max(0, flick.contentHeight - flick.height)))
    }
    Timer {
        interval: 300; repeat: true; running: true
        onTriggered: { try {
            if (++test.ticks > 200) throw new Error("deadline step=" + test.step)
            if (test.saving || test.ticks < test.waitUntil) return
            const root = test.appRoot
            if (test.step === 0) {
                if (test.ticks < 8) return
                test.require(root.page === "home", "the app opens on the home page")
                const line = test.find(test.window(), "home-sponsor")
                test.require(line && line.visible, "the home page shows the coffee line")
                const p = line.mapToItem(test.window(), 0, 0)
                test.require(p.x >= 16 && p.x + line.width <= test.window().width - 16, "the coffee line fits the window (" + Math.round(line.width) + " of " + Math.round(test.window().width) + ")")
                test.shot(test.window(), "home", 1); return
            }
            if (test.step === 1) {
                const line = test.find(test.window(), "home-sponsor")
                // On the link words at the line's right end, where a user aims.
                test.click("home", line, line.width - 30, line.height / 2)
                test.waitUntil = test.ticks + 6; test.step = 2; return
            }
            if (test.step === 2) {
                test.require(root.page === "set", "the coffee line opens 设置")
                test.require(test.settings.section === "sponsor", "on the 赞助作者 section")
                const panel = test.find(test.window(), "set-sponsor")
                test.require(panel && panel.visible && panel.height > 600, "the sponsor panel shows")
                for (const id of test.links) test.fits(test.find(panel, "sponsor-link-" + id), id + " card")
                for (const id of ["wechat", "alipay"]) {
                    test.fits(test.find(panel, "sponsor-code-" + id), id + " code")
                    test.require(test.find(panel, "sponsor-qr-" + id).status === Image.Ready, id + " code loaded")
                }
                test.fits(panel, "sponsor panel")
                const y = name => test.find(panel, name).mapToItem(panel, 0, 0).y
                test.require(y("sponsor-link-kofi") < y("sponsor-mainland") && y("sponsor-mainland") < y("sponsor-code-wechat"),
                             "Ko-fi comes first, then 中国大陆用户 and the codes")
                test.waitUntil = test.ticks + 2
                test.shot(test.window(), "settings", 3); return
            }
            if (test.step === 3) {
                test.shot(test.find(test.window(), "set-sponsor"), "panel", 31); return
            }
            // Each code card on its own, for the script to decode.
            if (test.step === 31 || test.step === 32) {
                const id = test.step === 31 ? "wechat" : "alipay"
                test.shot(test.find(test.window(), "sponsor-code-" + id), "code-" + id, test.step === 31 ? 32 : 4); return
            }
            if (test.step === 4) {
                // Every card and the Ko-fi button, one click each.
                const panel = test.find(test.window(), "set-sponsor")
                const card = test.find(panel, "sponsor-link-" + test.links[test.linkIndex])
                test.reveal(card)
                test.waitUntil = test.ticks + 2; test.step = 5; return
            }
            if (test.step === 5) {
                const card = test.find(test.find(test.window(), "set-sponsor"), "sponsor-link-" + test.links[test.linkIndex])
                test.click(test.links[test.linkIndex], card, card.width / 2, card.height - 22)
                test.waitUntil = test.ticks + 4
                test.step = ++test.linkIndex < test.links.length ? 4 : 6; return
            }
            if (test.step === 6) {
                test.require(root.page === "set" && test.settings.section === "sponsor", "the links leave the page where it was")
                test.settings.section = "about"
                test.waitUntil = test.ticks + 4; test.step = 7; return
            }
            if (test.step === 7) {
                const button = test.find(test.window(), "about-sponsor")
                test.require(button && button.visible, "关于 offers 赞助作者")
                test.reveal(button)
                test.waitUntil = test.ticks + 2; test.step = 8; return
            }
            if (test.step === 8) {
                const button = test.find(test.window(), "about-sponsor")
                test.click("about", button, button.width / 2, button.height / 2)
                test.waitUntil = test.ticks + 4; test.step = 9; return
            }
            if (test.step === 9) {
                test.require(test.settings.section === "sponsor", "关于 → 前往 opens 赞助作者")
                console.log("SPONSOR_UI_PASS")
                test.step = 10
                Qt.quit()
            }
        } catch (e) {
            const root = test.appRoot
            console.log("SPONSOR_UI_FAIL", e.message, "step=" + test.step, "link=" + test.linkIndex,
                        "page=" + (root ? root.page : "?"), "section=" + (test.settings ? test.settings.section : "?"),
                        "window=" + !!test.Window.window, "panel=" + !!test.find(test.window(), "set-sponsor"))
            test.step = 99
            Qt.quit()
        } }
    }
}
