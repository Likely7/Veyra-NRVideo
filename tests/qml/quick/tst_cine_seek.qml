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
        property var liveTimeshift: ({enabled: false, available: false, start: 0, end: 0})
        readonly property bool timelineSeekable: !isCapture && (duration > 0 || liveTimeshift.available)
        function timelineSeconds(fraction) {
            return liveTimeshift.available ? liveTimeshift.start + fraction * (liveTimeshift.end - liveTimeshift.start)
                                           : duration * fraction
        }
        function returnToLive() { seekTo(liveTimeshift.end) }
        function seekTo(seconds) { seeks = seeks.concat([seconds]) }
        function formatTime(seconds) { return String(Math.floor(seconds)) }
        function logUi(category, text) {}
        function setWindowMask(window, rect) {}
    }
    CineBar { id: bar; x: 50; y: 220; width: 860; height: 92 }
    SignalSpy { id: moveSpy; target: bar; signalName: "requestMove" }
    SignalSpy { id: hideSpy; target: bar; signalName: "requestHide" }
    TestCase {
        name: "CinemaSeek"
        when: windowShown
        function init() {
            root.Window.window.width = 1000
            root.Window.window.height = 650
            root.Window.window.requestActivate()
            veyra.seeks = []; veyra.duration = 100; veyra.isCapture = false
            veyra.hasSource = false
            veyra.currentPresetName = ""
            veyra.liveTimeshift = {enabled: false, available: false, start: 0, end: 0}
            bar.width = 860
            bar.fullscreen = true
            moveSpy.clear()
            hideSpy.clear()
        }
        function test_hide_fits_data() {
            const longPreset = "Long custom preset name ".repeat(8)
            return [
                {tag:"windowed-860", width:860, full:false, capture:true, preset:"自定义"},
                {tag:"file-760", width:760, full:true, capture:false, preset:longPreset},
                {tag:"file-819", width:819, full:true, capture:false, preset:longPreset},
                {tag:"file-820", width:820, full:true, capture:false, preset:longPreset}
            ]
        }
        function test_hide_fits(data) {
            bar.width = data.width
            bar.fullscreen = data.full
            veyra.hasSource = true
            veyra.isCapture = data.capture
            veyra.currentPresetName = data.preset
            verify(waitForRendering(bar))
            const hide = findChild(bar, "cine-hide")
            const rail = findChild(bar, "cine-seek")
            const end = hide.mapToItem(bar, hide.width, hide.height / 2)
            verify(hide.width >= 32 && end.x <= bar.width - 15,
                   "Hide hit area ends at " + end.x + " in a " + bar.width + " px pill")
            verify(rail.width > 100, "The other controls must fit without crowding out seek")
            mouseClick(hide, hide.width - 2, hide.height / 2)
            compare(hideSpy.count, 1)
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
