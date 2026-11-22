import QtQuick
import QtQuick.Window
import QtTest
import Veyra

Item {
    id: root
    width: 1000; height: 650
    QtObject {
        id: veyra
        property bool hasSource: false
        property bool isCapture: false
        property bool running: false
        property bool paused: true
        property bool failed: false
        property bool captureRecovering: false
        property real duration: 100
        property real progress: .3
        property real volume: .5
        readonly property real maximumVolume: 3
        property real playbackRate: 1
        property int previewRotation: 0
        property int thumbnailGeneration: 0
        property string sourceName: "Seek fixture"
        property string sourceSummary: ""
        property string positionText: "0:30"
        property string durationText: "1:40"
        property string currentPresetName: ""
        property int subtitlePrimary: -1
        property int selectedAudioTrack: -1
        property var presets: []
        property var subtitleTracks: []
        property var audioTracks: []
        property var seeks: []
        property var relativeSeeks: []
        property var stopRequests: []
        property var ps5: ({canEndStream:false, ending:false})
        property var liveTimeshift: ({enabled: false, available: false, start: 0, end: 0})
        readonly property bool timelineSeekable: !isCapture && (duration > 0 || liveTimeshift.available)
        function timelineSeconds(fraction) {
            return liveTimeshift.available ? liveTimeshift.start + fraction * (liveTimeshift.end - liveTimeshift.start)
                                           : duration * fraction
        }
        function returnToLive() { seekTo(liveTimeshift.end) }
        function seekTo(seconds) { seeks = seeks.concat([seconds]) }
        function seekBy(seconds) { relativeSeeks = relativeSeeks.concat([seconds]) }
        function ps5EndStream(standby) { stopRequests = stopRequests.concat([standby]); return true }
        function stopPlayback() { stopRequests = stopRequests.concat([false]) }
        function formatTime(seconds) { return String(Math.floor(seconds)) }
        function logUi(category, text) {}
        function setWindowMask(window, rect) {}
    }
    CineBar { id: bar; x: 50; y: 220; width: 860; height: 92 }
    SignalSpy { id: moveSpy; target: bar; signalName: "requestMove" }
    SignalSpy { id: dialogSpy; target: bar; signalName: "requestDialog" }
    TestCase {
        name: "CinemaSeek"
        when: windowShown
        function init() {
            root.Window.window.width = 1000
            root.Window.window.height = 650
            root.Window.window.requestActivate()
            veyra.seeks = []; veyra.duration = 100; veyra.isCapture = false
            veyra.relativeSeeks = []; veyra.stopRequests = []
            veyra.ps5 = {canEndStream:false, ending:false}
            veyra.liveTimeshift = {enabled: false, available: false, start: 0, end: 0}
            bar.fullscreen = true
            // Session changes replace a layout item. Settle that layout and
            // leave the prior test's seek preview before synthesizing presses.
            mouseMove(root, 1, 1)
            waitForPolish(bar)
            wait(30)
            moveSpy.clear()
            dialogSpy.clear()
        }
        function transportModes() {
            return [{tag:"fullscreen",full:true},{tag:"windowed",full:false}]
        }
        function glyphButton(item, glyph) {
            if (item.glyph === glyph) return item
            const children = item.children || []
            for (let i = 0; i < children.length; ++i) {
                const found = glyphButton(children[i], glyph)
                if (found) return found
            }
            return null
        }
        function test_ps5_stop_opens_choice_without_power_data() { return transportModes() }
        function test_ps5_stop_opens_choice_without_power(data) {
            bar.fullscreen = data.full
            const stop = findChild(bar, "ps5-end-stream")
            const forward = glyphButton(bar, "fwd10")
            verify(stop && forward)
            veyra.ps5 = {canEndStream:true, ending:false}
            tryCompare(stop, "visible", true)
            verify(stop.enabled)
            verify(!forward.visible, "The stop control replaces the forward transport slot")
            wait(30)
            mouseClick(stop, stop.width / 2, stop.height / 2)
            compare(dialogSpy.count, 1)
            compare(dialogSpy.signalArguments[0][0], "ps5-stop")
            compare(veyra.stopRequests.length, 0, "Opening the choice must not disconnect or send a power command")
            compare(veyra.relativeSeeks.length, 0)
            compare(moveSpy.count, 0, "Clicking the stop control must not move the window")
        }
        function test_ps5_ending_stays_visible_but_disabled_data() { return transportModes() }
        function test_ps5_ending_stays_visible_but_disabled(data) {
            bar.fullscreen = data.full
            // The real backend clears canEndStream while an end request runs.
            veyra.ps5 = {canEndStream:false, ending:true}
            const stop = findChild(bar, "ps5-end-stream")
            verify(stop)
            tryCompare(stop, "visible", true)
            verify(!stop.enabled)
            verify(!glyphButton(bar, "fwd10").visible)
            wait(30)
            mouseClick(stop, stop.width / 2, stop.height / 2)
            compare(dialogSpy.count, 0)
            compare(veyra.stopRequests.length, 0)
            compare(veyra.relativeSeeks.length, 0)
        }
        function test_no_ps5_restores_transport_and_seek_data() { return transportModes() }
        function test_no_ps5_restores_transport_and_seek(data) {
            bar.fullscreen = data.full
            const stop = findChild(bar, "ps5-end-stream")
            const forward = glyphButton(bar, "fwd10")
            verify(stop && forward)
            veyra.ps5 = {canEndStream:true, ending:false}
            tryCompare(stop, "visible", true)
            veyra.ps5 = {canEndStream:false, ending:false}
            tryCompare(stop, "visible", false)
            verify(forward.visible)
            wait(30)
            mouseClick(forward, forward.width / 2, forward.height / 2)
            compare(veyra.relativeSeeks.length, 1)
            compare(veyra.relativeSeeks[0], 10)
            const rail = findChild(bar, "cine-seek")
            mouseClick(rail, rail.width * .6, 7)
            fuzzyCompare(veyra.seeks[veyra.seeks.length-1], 60, 1)
            compare(dialogSpy.count, 0)
            compare(veyra.stopRequests.length, 0)
        }
        function test_scrub_data() {
            return [{tag:"fullscreen",full:true},{tag:"windowed",full:false}]
        }
        function test_scrub(data) {
            bar.fullscreen = data.full
            const rail = findChild(bar, "cine-seek")
            verify(rail !== null && rail.width > 100)
            mousePress(rail, rail.width * .2, 7)
            verify(bar.interacting, "A pressed seek rail must keep the control window active")
            for (let i = 1; i <= 6; ++i) mouseMove(rail, rail.width * (.2 + .1*i), 7, 30)
            mouseRelease(rail, rail.width * .8, 7)
            compare(bar.interacting, false)
            verify(veyra.seeks.length > 0)
            fuzzyCompare(veyra.seeks[veyra.seeks.length-1], 80, 1)
            compare(moveSpy.count, 0, "Scrubbing must never move the window")
        }
        function test_drag_outside_clamps_and_retains_preview() {
            const rail = findChild(bar, "cine-seek")
            mousePress(rail, rail.width * .5, 7)
            mouseMove(rail, rail.width + 25, -25, 20)
            verify(bar.interacting)
            verify(bar.seekPreviewOpen, "Keep the preview inside the native mask while dragging outside")
            mouseRelease(rail, rail.width + 25, -25)
            compare(bar.interacting, false)
            compare(veyra.seeks[veyra.seeks.length-1], 100)
        }
        function test_capture_does_not_seek() {
            veyra.isCapture = true
            const rail = findChild(bar, "cine-seek")
            mouseClick(rail, rail.width * .5, 7)
            compare(bar.interacting, false)
            compare(veyra.seeks.length, 0)
        }
        function test_click_seek() {
            const rail = findChild(bar, "cine-seek")
            mouseClick(rail, rail.width * .6, 7)
            fuzzyCompare(veyra.seeks[veyra.seeks.length-1], 60, 1)
            compare(bar.interacting, false)
        }
        function test_live_seek_and_return() {
            veyra.duration = 0
            veyra.liveTimeshift = {enabled: true, available: true, start: 10, end: 70}
            const rail = findChild(bar, "cine-seek")
            mouseClick(rail, rail.width * .5, 7)
            fuzzyCompare(veyra.seeks[0], 40, 0.5)
            const live = findChild(bar, "cine-return-live")
            verify(live && live.visible)
            mouseClick(live)
            compare(veyra.seeks[1], 70)
        }
        function test_cancel_stops_pending_seek() {
            const rail = findChild(bar, "cine-seek")
            mousePress(rail, rail.width * .2, 7)
            mouseMove(rail, rail.width * .7, 7, 10)
            veyra.isCapture = true // Disabling MouseArea cancels the in-flight grab.
            tryCompare(bar, "interacting", false)
            const count = veyra.seeks.length
            wait(180)
            compare(veyra.seeks.length, count)
            mouseRelease(rail, rail.width * .7, 7)
        }
    }
}
