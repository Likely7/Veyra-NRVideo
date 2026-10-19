import QtQuick
import QtTest
import Veyra

// 设置 → 赞助作者: every platform card and the Ko-fi button name their page, Ko-fi comes
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
            const ids = ["bilibili", "douyin", "x", "github", "youtube", "kofi"]
            for (let i = 0; i < ids.length; ++i) {
                const card = findChild(panel, "sponsor-link-" + ids[i])
                verify(card, ids[i])
                // On 前往主页 (or the Ko-fi button's text): the whole card is the button.
                mouseClick(card, card.width / 2, card.height - 22)
                compare(opened.count, i + 1, ids[i])
                compare(opened.signalArguments[i][0], ids[i])
            }
        }
        function test_assetsLoad() {
            const all = images(panel, [])
            // Five tiles, two payment marks, two codes and the Ko-fi cup.
            compare(all.length, 10)
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
        // Ko-fi first, then 中国大陆用户 with the two codes.
        function test_kofiBeforeMainlandCodes() {
            panel.width = 720
            wait(50)
            const y = name => findChild(panel, name).mapToItem(panel, 0, 0).y
            verify(y("sponsor-link-kofi") < y("sponsor-mainland"), "Ko-fi above the mainland block")
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
            for (const item of [last, alipay])
                verify(item.mapToItem(panel, item.width, 0).x <= panel.width + 0.5, item.objectName + " stays inside")
            panel.width = 720
        }
    }
}
