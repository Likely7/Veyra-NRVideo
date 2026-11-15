import QtQuick
import QtTest
import Veyra

// 设置 → 赞助作者: every platform card and the Patreon buttons name their page, Patreon comes
// before the mainland China codes, and the brand tiles and both codes load (SVGs need
// the qsvg plugin beside the app).
Item {
    width: 760; height: 1000
    SponsorPanel { id: panel; width: 720 }
    SignalSpy { id: opened; target: panel; signalName: "openLink" }
    TestCase {
        name: "SponsorPanel"
        when: windowShown
        function cleanup() { opened.clear() }
        // The panel's own images (a VIcon also holds one for the software renderer).
        function images(item, out) {
            if (item instanceof Image && item.objectName !== "software-icon-image") out.push(item)
            for (let i = 0; i < item.children.length; ++i) images(item.children[i], out)
            return out
        }
        function test_cardsOpenTheirPage() {
            const ids = ["bilibili", "douyin", "x", "github", "youtube", "patreon", "patreonCoffee"]
            for (let i = 0; i < ids.length; ++i) {
                const card = findChild(panel, "sponsor-link-" + ids[i])
                verify(card, ids[i])
                // On 前往主页 (or the Patreon button's text): the whole card is the button.
                mouseClick(card, card.width / 2, card.height - 22)
                compare(opened.count, i + 1, ids[i])
                compare(opened.signalArguments[i][0], ids[i])
            }
        }
        function test_assetsLoad() {
            const all = images(panel, [])
            // Five tiles, two payment marks, two codes and the two Patreon marks.
            compare(all.length, 11)
            for (const image of all) {
                tryCompare(image, "status", Image.Ready, 3000, image.source)
                verify(image.paintedWidth > 0, image.source)
            }
            for (const id of ["wechat", "alipay"]) {
                const code = findChild(panel, "sponsor-qr-" + id)
                // Shown 220 px wide: large enough for a phone to read.
                compare(Math.round(code.paintedWidth), 220, id)
            }
        }
        // Patreon first, then 中国大陆用户 with the two codes.
        function test_patreonBeforeMainlandCodes() {
            panel.width = 720
            wait(50)
            const y = name => findChild(panel, name).mapToItem(panel, 0, 0).y
            for (const id of ["patreon", "patreonCoffee"])
                verify(y("sponsor-link-" + id) < y("sponsor-mainland"), id + " above the mainland block")
            verify(y("sponsor-mainland") < y("sponsor-code-wechat"), "the mainland title above the codes")
            compare(y("sponsor-code-wechat"), y("sponsor-code-alipay"))
        }
        function test_narrowWraps() {
            panel.width = 410
            wait(50)
            const first = findChild(panel, "sponsor-link-bilibili"), last = findChild(panel, "sponsor-link-youtube")
            verify(last.mapToItem(panel, 0, 0).y > first.mapToItem(panel, 0, 0).y, "cards wrap onto a second row")
            const wechat = findChild(panel, "sponsor-code-wechat"), alipay = findChild(panel, "sponsor-code-alipay")
            verify(alipay.mapToItem(panel, 0, 0).y > wechat.mapToItem(panel, 0, 0).y, "codes stack")
            const home = findChild(panel, "sponsor-link-patreon")
            const coffee = findChild(panel, "sponsor-link-patreonCoffee")
            verify(coffee.mapToItem(panel, 0, 0).y > home.mapToItem(panel, 0, 0).y, "Patreon buttons wrap")
            for (const item of [last, alipay, home, coffee])
                verify(item.mapToItem(panel, item.width, 0).x <= panel.width + 0.5, item.objectName + " stays inside")
            panel.width = 720
        }
    }
}
