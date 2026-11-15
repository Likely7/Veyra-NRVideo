// The 日落 (dusk) palette's startup intro, 2026-11-15. 3.26 s, cut to the opening of the
// startup music (StartupMusic.h: media/startup-music.mp3, measured onsets 0.16, 0.64, 0.79,
// 0.88, 1.12 s - the strongest - and 3.26 s):
//   0.16-1.12 s the user's wordmark comes out of the dark as three paper layers at different
//               depths - N behind, R, then the word "Veyra" in front. Each drifts in from
//               close to the camera, then rushes in, accelerating, and stops dead on its beat
//               (N 0.64, R 0.79, Veyra 1.12) with a short squash-and-rebound; the camera
//               kicks on each landing and shakes on the last;
//   1.12-2.7 s  the horizon ignites on that hit, the sky warms, a striped sun rises, embers
//               drift up; the camera keeps pushing in and drifting sideways, so the layers
//               keep sliding over each other;
//   2.7-3.26 s  the layers glide to the wordmark's place on the home page, the nearer ones
//               trailing, and meet there on the 3.26 s beat; the sun sets into the sea and
//               the scene dissolves into the page.
// The user asked for a harder attack than ease-in-ease-out: the landings accelerate into the
// beat and stop dead. Film grain and a vignette sit over the scene; no white flashes, sweeps
// or lens streaks (the user found those plastic). A click or a key skips: before "Veyra" has
// landed the scene fades out at once, after it the glide runs 2.5x faster. Dragging moves
// the window.
//
// One clock (`ms`) drives every frame: each property below is a function of it, so a probe
// can stop the clock and sample any moment. With the music playing the clock is the music's
// position (followMusic), so the beats stay on the sound; the first frame is held until the
// music starts (0.7 s at most). Without music, a stalled frame slows the clock instead of
// jumping it. Nothing keeps running afterwards: Main.qml unloads the item on `finished`.
//
// The software scene graph (the default interface renderer) has no shader effects, so this
// is only Image, Rectangle, clip, opacity and transforms. The layers, glows and soft edges
// are images baked by tools/qt_probe/make-wordmark-dusk.py and make-intro-dusk.py.
import QtQuick

Item {
    id: intro
    // The home page's wordmark in this item's coordinates; Main.qml refreshes it on `frame`.
    property rect target: Qt.rect(width / 2 - 115, height * 0.2, 230, 192)
    property string skipText: ""
    property real ms: 0
    property bool running: false
    property real speed: 1
    property bool dismissing: false
    // The music's position in ms, or -1 while it is not playing (Main.qml, StartupMusic).
    property var musicClock: null
    property bool followMusic: false
    property real waited: 0
    // For the log line Main.qml writes when the intro ends.
    property int frames: 0
    property real worstFrameMs: 0
    readonly property real hitN: 639
    readonly property real hitR: 789
    readonly property real hitV: 1115
    readonly property real handoffAt: 2700
    readonly property real total: 3262
    readonly property real landedAt: hitV
    // Before each clock step, so the glide aims at where the home wordmark is now.
    signal frame()
    // The home page's own wordmark may show: under the last frame of the glide, or under
    // the fading scene when the intro is dismissed.
    signal revealHome()
    signal finished()

    function start() { running = true; forceActiveFocus() }
    function skip() {
        if (dismissing || ms >= total) return
        if (ms < landedAt) { dismiss(); return }
        if (ms < handoffAt) ms = handoffAt
        followMusic = false
        speed = 2.5
    }
    // Fade out wherever the intro is (a skip before the landing, or the page changed).
    function dismiss() {
        if (dismissing) return
        dismissing = true
        revealHome()
        fadeOut.start()
    }
    NumberAnimation { id: fadeOut; target: intro; property: "opacity"; to: 0; duration: 220; easing.type: Easing.OutCubic; onFinished: intro.finished() }
    FrameAnimation {
        running: intro.running && !intro.dismissing
        onTriggered: {
            intro.frame()
            ++intro.frames
            if (intro.frames > 2) intro.worstFrameMs = Math.max(intro.worstFrameMs, frameTime * 1000)
            const step = Math.min(frameTime * 1000, 34)
            let next = intro.ms + step * intro.speed
            if (intro.followMusic) {
                const music = intro.musicClock ? intro.musicClock() : -1
                if (music >= 0) next = Math.max(intro.ms, music)          // the music leads
                else if ((intro.waited += step) < 700) next = intro.ms     // not started yet: hold
                else intro.followMusic = false                             // no sound: run on frames
            }
            intro.ms = Math.min(intro.total, next)
            if (intro.ms >= intro.total) {
                intro.running = false
                intro.revealHome()
                intro.finished()
            }
        }
    }

    // --- timeline ----------------------------------------------------------------
    function clamp01(x) { return x < 0 ? 0 : x > 1 ? 1 : x }
    function span(t, a, b) { return clamp01((t - a) / (b - a)) }
    function at(a, b) { return span(ms, a, b) }
    function mix(a, b, t) { return a + (b - a) * t }
    function mixc(a, b, t) { return Qt.rgba(mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), 1) }
    function outCubic(t) { return 1 - Math.pow(1 - t, 3) }
    function outExpo(t) { return t >= 1 ? 1 : 1 - Math.pow(2, -10 * t) }
    function inOutCubic(t) { return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2 }
    function inOutQuart(t) { return t < 0.5 ? 8 * t * t * t * t : 1 - Math.pow(-2 * t + 2, 4) / 2 }
    function inOutSine(t) { return (1 - Math.cos(Math.PI * t)) / 2 }
    // A fixed pseudo-random number per ember and key: the same intro every time.
    function rnd(i, k) { const x = Math.sin(i * 127.1 + k * 311.7) * 43758.5453; return x - Math.floor(x) }
    // A landing on a beat, as extra scale: a slow drift in from k0 (close to the camera),
    // then a rush that accelerates (cubic) and stops dead at `land`, then a thud - a squash
    // and a rebound dying out in about 0.3 s.
    function landing(t, appear, land, rush, k0, thud) {
        if (t >= land) {
            const u = t - land
            return -thud * Math.exp(-u / 110) * Math.sin(2 * Math.PI * u / 240)
        }
        const k1 = k0 * 0.8
        const r0 = land - rush
        if (t < r0) return mix(k0, k1, span(t, appear, r0))
        const v = span(t, r0, land)
        return k1 * (1 - v * v * v)
    }
    // The camera's kick on a beat: a jump in and a damped swing back.
    function kick(t, beat, amount) {
        if (t < beat) return 0
        const u = t - beat
        return amount * Math.exp(-u / 150) * Math.cos(2 * Math.PI * u / 320)
    }
    function shake(t, beat, px, period, phase) {
        if (t < beat) return 0
        const u = t - beat
        return px * Math.exp(-u / 90) * Math.sin(2 * Math.PI * u / period + phase)
    }
    readonly property real camera: 1 + kick(ms, hitN, 0.012) + kick(ms, hitR, 0.01) + kick(ms, hitV, 0.035)
    readonly property real shakeX: shake(ms, hitN, 1.5, 46, 0) + shake(ms, hitV, 3.5, 46, 0)
    readonly property real shakeY: shake(ms, hitN, 1, 38, 1) + shake(ms, hitV, 2.5, 38, 1)

    readonly property real horizonOn: outExpo(at(hitV, hitV + 320))
    readonly property real warm: inOutSine(at(hitV, 2600))                 // night -> dusk sky
    readonly property real glide: inOutQuart(at(handoffAt, total))
    // The sun rises behind the wordmark, and sets into the sea as the wordmark glides away.
    readonly property real sunUp: Math.max(0, outCubic(at(1250, 2650)) - inOutCubic(at(handoffAt, handoffAt + 450)))
    // The glide dissolves the light first, into the sky, then the sky into the page.
    readonly property real lights: 1 - inOutSine(at(handoffAt + 50, handoffAt + 380))
    readonly property real scene: 1 - inOutSine(at(handoffAt + 120, total))

    // --- layout: the wordmark stands on the horizon, a little above the middle ---------
    readonly property real aspect: 1164 / 969                      // wordmark-dusk.png
    readonly property real glowPad: 186 / 1164                     // the glow's margin
    readonly property real heroH: Math.round(Math.min(height * 0.44, width * 0.5 / aspect))
    readonly property real heroW: Math.round(heroH * aspect)
    readonly property real horizonY: Math.round(height * 0.43 + heroH / 2)
    readonly property real sunD: Math.round(heroH * 1.42)
    readonly property real sunCentreY: mix(horizonY + sunD * 0.55, horizonY - sunD * 0.30, sunUp)
    // A layer's box at time t. depth: 0 far (N) .. 1 near ("Veyra"); lag: how much later it
    // sets off on the glide. Near layers come from closer to the camera, move more in the push
    // and the drift, and trail on the glide. Scaled about the wordmark's foot on the horizon.
    function layerRect(t, depth, appear, land, rush, k0, thud, lag) {
        const g = inOutQuart(span(t, handoffAt + lag, total))
        const hold = inOutSine(span(t, hitV, handoffAt)) * (1 - g)
        const k = (1 + landing(t, appear, land, rush, k0, thud)) * (1 + 0.024 * (1 + 1.3 * depth) * hold) * (1 + (camera - 1) * (1 - g))
        const w = heroW * k, h = heroH * k
        const x = width / 2 - w / 2 - heroW * 0.014 * depth * hold + shakeX * (0.6 + 0.4 * depth)
        const y = horizonY - h - heroH * 0.006 * depth * hold + shakeY * (0.6 + 0.4 * depth)
        return Qt.rect(mix(x, target.x, g), mix(y, target.y, g), mix(w, target.width, g), mix(h, target.height, g))
    }
    readonly property rect backRect: layerRect(ms, 0, 160, hitN, 150, 0.42, 0.022, 0)
    // The sun's stripes: gaps below its middle that drift down and widen as they go.
    function gapCentre(k) { return 0.45 + (k + (ms / 1800) % 1) * 0.085 }
    function gapSize(k) { return 0.006 + 0.055 * Math.pow(clamp01((gapCentre(k) - 0.45) / 0.55), 1.3) }

    // --- colours: night, then the dusk palette's sky ------------------------------------
    readonly property color night0: "#07060D"
    readonly property color night1: "#0B0916"
    readonly property color night2: "#120E22"
    readonly property color night3: "#1B1430"
    readonly property color night4: "#2A1A3A"
    readonly property color dusk0: "#120F24"
    readonly property color dusk1: "#2A1C46"
    readonly property color dusk2: "#5B2C5E"
    readonly property color dusk3: "#9A4670"
    readonly property color dusk4: "#C9677E"
    readonly property color seaNight0: "#0F0B1A"
    readonly property color seaNight1: "#060509"
    readonly property color seaDusk0: "#3A2148"
    readonly property color seaDusk1: "#0D0B19"

    // --- input: every press and key goes to the intro --------------------------------------
    focus: true
    Keys.onPressed: event => { intro.skip(); event.accepted = true }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true          // the dock stays up while the intro shows
        acceptedButtons: Qt.AllButtons
        property point pressAt
        property bool moved: false
        onPressed: mouse => { pressAt = Qt.point(mouse.x, mouse.y); moved = false }
        onPositionChanged: mouse => {
            if (!pressed || moved || Math.hypot(mouse.x - pressAt.x, mouse.y - pressAt.y) < 6) return
            moved = true
            const w = intro.Window.window
            if (w && w.visibility === Window.Windowed) w.startSystemMove()
        }
        onClicked: if (!moved) intro.skip()
        onWheel: wheel => wheel.accepted = true
    }

    // --- sky and sea: one opaque fill with the window's own corners (the GPU interface
    // renderer leaves them transparent), a hard edge at the horizon -------------------------
    Rectangle {
        id: fill
        readonly property real h: intro.horizonY / Math.max(1, intro.height)
        anchors.fill: parent
        opacity: intro.scene
        radius: Theme.rWindow
        gradient: Gradient {
            GradientStop { position: 0; color: intro.mixc(intro.night0, intro.dusk0, intro.warm) }
            GradientStop { position: 0.45 * fill.h; color: intro.mixc(intro.night1, intro.dusk1, intro.warm) }
            GradientStop { position: 0.75 * fill.h; color: intro.mixc(intro.night2, intro.dusk2, intro.warm) }
            GradientStop { position: 0.92 * fill.h; color: intro.mixc(intro.night3, intro.dusk3, intro.warm) }
            GradientStop { position: fill.h; color: intro.mixc(intro.night4, intro.dusk4, intro.warm) }
            GradientStop { position: fill.h + 0.0005; color: intro.mixc(intro.seaNight0, intro.seaDusk0, intro.warm) }
            GradientStop { position: 1; color: intro.mixc(intro.seaNight1, intro.seaDusk1, intro.warm) }
        }
    }

    // --- the sun, above the horizon only -------------------------------------------------
    Item {
        width: intro.width
        height: intro.horizonY
        clip: true
        opacity: intro.lights
        visible: intro.sunUp > 0
        Image {
            x: (intro.width - width) / 2
            y: intro.sunCentreY - height / 2
            width: intro.sunD * 1.5
            height: width
            source: "intro-sun-glow.png"
            asynchronous: true
            opacity: intro.clamp01(intro.sunUp * 1.25) * 0.75
        }
        Item {
            id: sunBox
            x: (intro.width - intro.sunD) / 2
            y: intro.sunCentreY - intro.sunD / 2
            width: intro.sunD
            height: intro.sunD
            opacity: 0.92
            // Eight bands of the same disc with the gaps between them.
            Repeater {
                model: 8
                Item {
                    required property int index
                    readonly property real v0: index === 0 ? 0 : Math.min(1, intro.gapCentre(index - 1) + intro.gapSize(index - 1) / 2)
                    readonly property real v1: index === 7 ? 1 : Math.min(1, intro.gapCentre(index) - intro.gapSize(index) / 2)
                    y: v0 * sunBox.height
                    width: sunBox.width
                    height: Math.max(0, v1 - v0) * sunBox.height
                    visible: height > 0.5
                    clip: true
                    Image {
                        y: -parent.y
                        width: sunBox.width
                        height: sunBox.height
                        source: "intro-sun.png"
                        sourceSize: Qt.size(sunBox.width, sunBox.height)
                        asynchronous: true
                    }
                }
            }
        }
    }

    // --- its reflection on the water ---------------------------------------------------------
    Item {
        y: intro.horizonY
        width: intro.width
        height: intro.height - intro.horizonY
        clip: true
        opacity: intro.lights
        visible: intro.sunUp > 0
        Image {
            x: (intro.width - width) / 2
            y: -height * 0.32
            width: intro.sunD * 1.4
            height: intro.sunD * 0.42
            source: "intro-sun-glow.png"
            asynchronous: true
            opacity: intro.sunUp * 0.28
        }
        Repeater {
            model: 7
            Rectangle {
                required property int index
                readonly property color tone: index % 2 ? "#EF8FB0" : "#F6B98A"
                width: intro.sunD * (0.55 - index * 0.045) * intro.sunUp * (0.92 + 0.08 * Math.sin(intro.ms * 0.005 + index * 1.9))
                height: 1.5 + index * 0.6
                x: (intro.width - width) / 2 + Math.sin(intro.ms * 0.0031 + index * 2.3) * intro.sunD * 0.02
                y: 6 + (intro.height - intro.horizonY) * 0.55 * Math.pow((index + 1) / 7, 1.6)
                opacity: (0.7 - index * 0.075) * intro.sunUp
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: tone }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }
    }

    // --- the horizon, ignited on the strongest beat -------------------------------------------
    Image {
        width: intro.width * intro.mix(0.25, 0.96, intro.horizonOn)
        height: 80
        x: (intro.width - width) / 2
        y: intro.horizonY - height / 2
        source: "intro-horizon.png"
        asynchronous: true
        // A brief overdrive on the hit, then its resting brightness.
        opacity: intro.clamp01(intro.horizonOn * 4) * (0.8 + 0.2 * Math.exp(-Math.max(0, intro.ms - intro.hitV) / 180)) * intro.lights
        visible: intro.ms >= intro.hitV
    }

    // --- embers drifting up over the water ---------------------------------------------------------
    Repeater {
        model: 18
        Rectangle {
            required property int index
            readonly property real born: 1250 + intro.rnd(index, 1) * 1300
            readonly property real life: 1000 + intro.rnd(index, 2) * 800
            readonly property real age: intro.ms - born
            readonly property real lift: intro.heroH / 350
            width: 1.5 + intro.rnd(index, 8) * 2
            height: width
            radius: width / 2
            color: intro.rnd(index, 9) < 0.4 ? "#F8C79A" : intro.rnd(index, 9) < 0.75 ? "#F29AC0" : "#CDB6F8"
            visible: age > 0 && age < life
            x: intro.width / 2 + (intro.rnd(index, 3) - 0.5) * intro.sunD * 1.5
               + (3 + intro.rnd(index, 5) * 10) * Math.sin(age * (0.002 + intro.rnd(index, 6) * 0.002) + intro.rnd(index, 7) * 6.28)
            y: intro.horizonY - 6 - intro.rnd(index, 10) * intro.heroH * 0.3 - (30 + intro.rnd(index, 4) * 60) * lift * age / 1000
            opacity: Math.sin(Math.PI * intro.clamp01(age / life)) * (0.3 + 0.4 * intro.rnd(index, 11)) * intro.lights
        }
    }

    // --- the wordmark ------------------------------------------------------------------------------
    // A soft glow behind it, lit with the sky; it ends at the home page's resting glow (0.16).
    Image {
        x: intro.backRect.x - intro.backRect.width * intro.glowPad
        y: intro.backRect.y - intro.backRect.width * intro.glowPad
        width: intro.backRect.width * (1 + 2 * intro.glowPad)
        height: intro.backRect.height + 2 * intro.backRect.width * intro.glowPad
        source: "wordmark-dusk-glow.png"
        asynchronous: true
        opacity: intro.ms < intro.handoffAt ? 0.42 * intro.outCubic(intro.at(intro.hitV, 2400)) : intro.mix(0.42, 0.16, intro.glide)
        visible: opacity > 0
    }
    // The paper layers, far to near, each decoded at its resting size. Over them, at the end of
    // the glide, the whole wordmark decoded at the home page's size fades in, so the last frame
    // is the home wordmark's own pixels.
    Repeater {
        model: [{ src: "wordmark-dusk-n.png", depth: 0, appear: 160, land: 639, rush: 150, k0: 0.42, thud: 0.022, lag: 0 },
                { src: "wordmark-dusk-r.png", depth: 0.35, appear: 330, land: 789, rush: 140, k0: 0.5, thud: 0.022, lag: 25 },
                { src: "wordmark-dusk-veyra.png", depth: 1, appear: 600, land: 1115, rush: 190, k0: 0.75, thud: 0.03, lag: 55 }]
        Image {
            required property var modelData
            readonly property rect box: intro.layerRect(intro.ms, modelData.depth, modelData.appear, modelData.land, modelData.rush,
                                                        modelData.k0, modelData.thud, modelData.lag)
            x: box.x
            y: box.y
            width: box.width
            height: box.height
            source: modelData.src
            sourceSize: Qt.size(Math.round(intro.heroW * 1.04), Math.round(intro.heroH * 1.04))
            asynchronous: true
            // Out of the dark while it drifts in; fully there before it lands.
            opacity: intro.outCubic(intro.at(modelData.appear, modelData.land - modelData.rush * 0.5))
            visible: opacity > 0
        }
    }
    Image {
        x: intro.target.x
        y: intro.target.y
        width: intro.target.width
        height: intro.target.height
        source: "wordmark-dusk.png"
        sourceSize: Qt.size(Math.round(intro.target.width), Math.round(intro.target.height))
        asynchronous: true
        opacity: intro.at(intro.total - 90, intro.total)
        visible: opacity > 0
    }

    // --- film: grain over everything, a vignette over the scene ----------------------------------
    Image {
        anchors.fill: parent
        source: "intro-vignette.png"
        asynchronous: true
        opacity: intro.scene
    }
    Item {
        anchors.fill: parent
        clip: true
        opacity: 0.22 * intro.scene
        Image {
            // A new grain offset every 42 ms (24 per second), as film grain changes per frame.
            readonly property int grainFrame: Math.floor(intro.ms / 42)
            x: -((grainFrame * 97) % 256)
            y: -((grainFrame * 181) % 256)
            width: intro.width + 256
            height: intro.height + 256
            source: "intro-grain.png"
            fillMode: Image.Tile
            asynchronous: true
        }
    }

    // Bottom left: the bottom right corner holds Main.qml's music button.
    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 18
        text: intro.skipText
        color: Theme.t1
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        opacity: 0.4 * intro.at(400, 900) * intro.scene
        visible: text.length > 0 && opacity > 0
    }
}
