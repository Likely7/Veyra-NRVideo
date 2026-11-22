import QtQuick
import QtCore
import QtQuick.Dialogs
import "PortraitGallery.js" as Gallery

Item {
    id: background

    required property bool active
    required property real pictureAspect
    required property real pixelRatio
    required property bool settingsOpen

    readonly property var gallery: Gallery.files
    readonly property string mode: preferences.mode
    readonly property int brightness: Math.max(0, Math.min(100, preferences.brightness))
    readonly property var words: Gallery.words(Qt.uiLanguage)
    property var slidePair: []
    property bool pickingLeft: true

    readonly property int physicalWidth: Math.round(width * pixelRatio)
    readonly property int physicalHeight: Math.round(height * pixelRatio)
    readonly property real gap: (physicalWidth - pictureAspect * physicalHeight) / 2
    readonly property bool eligible: active && physicalHeight > 0 && pictureAspect > 0
        && physicalWidth > physicalHeight * (16 / 9) + 2 && gap > 3

    // The native cover collector rounds its holes outwards. Keep both inner
    // edges inside the bars even when the monitor uses fractional DPI scaling.
    readonly property int leftEdge: Math.floor(gap - 0.5) - 1
    readonly property int rightEdge: Math.ceil(physicalWidth - gap + 1)

    function builtIn(left) {
        const name = left ? preferences.builtInLeft : preferences.builtInRight
        return gallery.includes(name) ? name : gallery[left ? 0 : 1] || gallery[0]
    }
    function galleryUrl(name) {
        if (!name) return ""
        return Qt.resolvedUrl("portraits/" + encodeURIComponent(name))
    }
    function sideUrl(left) {
        if (mode === "builtin") return galleryUrl(builtIn(left))
        if (mode === "slideshow") return galleryUrl(slidePair[left ? 0 : 1])
        if (mode === "custom") {
            const url = left ? preferences.customLeft : preferences.customRight
            return url.toString().startsWith("file:") ? url : ""
        }
        return ""
    }
    function setMode(mode) { preferences.mode = mode }
    function setBrightness(level) { preferences.brightness = Math.max(0, Math.min(100, Math.round(level))) }
    function setBuiltIn(left, name) {
        if (left) preferences.builtInLeft = name
        else preferences.builtInRight = name
    }
    function chooseCustom(left) {
        pickingLeft = left
        picker.open()
    }
    function nextPair() {
        // With three or more images, each side changes and the pair is distinct.
        const count = gallery.length
        if (count < 2) { slidePair = [gallery[0], gallery[0]]; return }
        if (count === 2 && slidePair.length) { slidePair = [slidePair[1], slidePair[0]]; return }
        const leftPool = gallery.filter(name => name !== slidePair[0])
        const left = leftPool[Math.floor(Math.random() * leftPool.length)]
        const rightPool = gallery.filter(name => name !== left && name !== slidePair[1])
        slidePair = [left, rightPool[Math.floor(Math.random() * rightPool.length)]]
    }
    Component.onCompleted: nextPair()
    Timer {
        interval: 20000
        repeat: true
        running: background.mode === "slideshow" && background.gallery.length > 1
            && (background.eligible || background.settingsOpen)
        onTriggered: background.nextPair()
    }

    Settings {
        id: preferences
        location: {
            const path = veyra.dataDirectory.replace(/\\/g, '/')
            const encoded = path.split('/').map(encodeURIComponent).join('/')
            const directory = path.startsWith('//') ? "file:" + encoded
                : "file:///" + encoded.replace(/^([A-Za-z])%3A/, '$1:')
            return directory + "/fullscreen-background.ini"
        }
        property string mode: "black"
        property int brightness: 60
        property string builtInLeft: Gallery.files[0] || ""
        property string builtInRight: Gallery.files[1] || Gallery.files[0] || ""
        property url customLeft: ""
        property url customRight: ""
    }

    FileDialog {
        id: picker
        title: background.pickingLeft ? background.words.left : background.words.right
        nameFilters: [background.words.images + " (*.png *.jpg *.jpeg *.bmp)"]
        onAccepted: {
            if (background.pickingLeft) preferences.customLeft = selectedFile
            else preferences.customRight = selectedFile
        }
    }

    Repeater {
        model: 2
        delegate: Rectangle {
            required property int index
            readonly property bool isLeft: index === 0
            objectName: isLeft ? "fullscreenBackgroundLeft" : "fullscreenBackgroundRight"
            readonly property bool videoCover: true
            readonly property bool passiveVideoCover: true
            x: isLeft ? 0 : background.rightEdge / background.pixelRatio
            width: Math.max(0, isLeft ? background.leftEdge / background.pixelRatio : background.width - x)
            height: background.height
            visible: background.eligible && background.mode !== "black" && picture.status !== Image.Error
                && picture.source.toString().length > 0
            color: "black"
            clip: true
            Image {
                id: picture
                anchors.fill: parent
                source: background.eligible ? background.sideUrl(parent.isLeft) : ""
                sourceSize.height: Math.max(360, Math.ceil(background.physicalHeight / 360) * 360)
                fillMode: Image.PreserveAspectCrop
                autoTransform: true
                asynchronous: true
                retainWhileLoading: true
                cache: false
                opacity: background.brightness / 100
            }
        }
    }
}
