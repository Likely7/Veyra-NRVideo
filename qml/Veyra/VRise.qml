// The design's [data-in] entrance (app.css .page.enter [data-in]):
//   animation: rise .6s var(--spring-soft) both; animation-delay: calc(var(--d) * 45ms)
//   @keyframes rise { from { opacity: 0; transform: translateY(16px) scale(.98) } }
// Declared in a VPage: `VRise { target: head; d: 1 }`, or inside a delegate with
// `page:` set. It plays on the page's enter(), which only an animated switch emits
// (core.js app.go(id, instant): not on the first show).
// "both": the item holds the from-state through its delay. The transforms are the
// VRise's own, so the item's own y, scale and hover motion are left alone.
import QtQuick

Item {
    id: rise
    required property Item target
    property int d: 0
    property Item page: parent
    visible: false
    Connections { target: rise.page; function onEnter() { rise.play() } }

    // The item's resting opacity, captured once: some items rest below 1.
    property real rest: 1
    Component.onCompleted: {
        rest = target.opacity
        target.transform = [sc, tr]
    }

    Scale {
        id: sc
        origin.x: rise.target.width / 2
        origin.y: rise.target.height / 2
        xScale: 1
        yScale: 1
    }
    Translate { id: tr }

    // For the motion probe.
    readonly property real ty: tr.y

    function play() {
        anim.stop()
        if (Theme.reduced) { target.opacity = rest; tr.y = 0; sc.xScale = sc.yScale = 1; return }
        target.opacity = 0; tr.y = 16; sc.xScale = sc.yScale = 0.98
        anim.start()
    }
    SequentialAnimation {
        id: anim
        PauseAnimation { duration: rise.d * 45 }
        ParallelAnimation {
            NumberAnimation { target: rise.target; property: "opacity"; to: rise.rest; duration: 600; easing.bezierCurve: Theme.springSoft }
            NumberAnimation { target: tr; property: "y"; to: 0; duration: 600; easing.bezierCurve: Theme.springSoft }
            NumberAnimation { target: sc; properties: "xScale,yScale"; to: 1; duration: 600; easing.bezierCurve: Theme.springSoft }
        }
    }
}
