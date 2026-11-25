import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "Ps5Bindings.js" as Bindings

Pane {
    id: page
    required property var settings
    signal dismissRequested(bool resume)
    signal practiceRequested()
    property bool listening: false
    objectName: "ps5-keyboard-editor"
    readonly property bool compact: width < 780
    property int section: 0
    readonly property var sections: [qsTr("输入绑定"), qsTr("鼠标与瞄准"), qsTr("情境"), qsTr("设备与快捷键"), qsTr("配置与游戏设置")]
    readonly property alias bodyViewport: scroll
    padding: 0
    font.family: Theme.fontUi
    font.pixelSize: Theme.fsBody
    palette.window: Theme.dialog
    palette.windowText: Theme.t1
    palette.text: Theme.t1
    palette.button: Theme.card3
    palette.buttonText: Theme.t1
    palette.base: Theme.card2
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentInk
    background: Rectangle { color: Theme.dialog }
    Component.onCompleted: resetView()
    onSectionChanged: { scroll.contentY = 0; listening = false }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut; enabled: !page.listening && !shortcutSettings.listening && !page.settings.shortcutConflict.key; onActivated: page.dismissRequested(false) }
    property var doc: settings ? settings.profile : ({})
    property string scene: "on_foot"
    property string selectedSpec: "KeyF"
    property string filterControl: ""
    property string pending: ""
    property bool baseEdit: false
    property int viewMode: 2
    readonly property var rows: Bindings.records(doc,scene)
    readonly property var chosen: rows.find(r => r.spec === selectedSpec)
    readonly property var selectedGestures: chosen ? rows.filter(row => row.key === chosen.key) : []
    function shortcutSituation(key) {
        return Object.keys(doc.context_hotkeys || {}).find(context => doc.context_hotkeys[context] === key) || ""
    }
    readonly property string selectedShortcut: shortcutSituation(selectedSpec)
    readonly property string pendingSituation: shortcutSituation(pending)
    readonly property var destinations: Bindings.keyboard.filter(key => key.bindable).map(key => key.key)
        .concat(Bindings.extraKeys, Bindings.mouse)
        .filter(key => !Object.values(doc.context_hotkeys || {}).includes(key))
    readonly property var scopeRows: baseEdit ? Bindings.records(doc,"") : rows
    readonly property var occupied: scopeRows.filter(r => r.key === pending)
    function resetView() {
        const contexts=doc.contexts || []
        scene=contexts.indexOf(settings.situation)>=0 ? settings.situation : (contexts[0] || "")
        selectedSpec="KeyF";filterControl="";pending="";baseEdit=false;viewMode=2;listening=false;search.clear()
        scroll.contentY=0
        let name = settings.selected
        if (name === settings.profiles[0]) {
            const base = qsTr("我的 GTA VI")
            name = base
            for (let suffix = 2; settings.profiles.includes(name); ++suffix)
                name = base + " (" + suffix + ")"
        }
        copyName.text = name
    }
    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            if (target && target.visible) page.revealFocus(target.activeFocusItem)
        }
    }
    function revealFocus(item) {
        if (!item || !item.activeFocus || !item.visualFocus) return
        let ancestor = item
        while (ancestor && ancestor !== scroll.contentItem) ancestor = ancestor.parent
        if (!ancestor) return
        const y = item.mapToItem(scroll.contentItem, 0, 0).y
        if (y < scroll.contentY) scroll.contentY = Math.max(0, y - 4)
        else if (y + item.height > scroll.contentY + scroll.height)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height), y + item.height - scroll.height + 4)
    }
    function selectInput(key) {
        listening=false;pending="";filterControl="";baseEdit=false
        const r=rows.find(row => row.key===key)
        selectedSpec=r?r.spec:key
    }
    function selectControl(control) {
        filterControl = control
        pending = ""
        baseEdit = false
        listening = false
        const row = chosen && chosen.controls.includes(control)
            ? chosen : rows.find(binding => binding.controls.includes(control))
        selectedSpec = row ? row.spec : ""
    }
    function destination(key) {
        if(!chosen || chosen.fixed || !key || key===chosen.key || !scopeRows.some(r=>r.spec===selectedSpec && r.action===chosen.action))return
        pending=key
        if(!baseEdit && !occupied.length)applyMove("replace")
    }
    function applyMove(choice) {
        if(choice==="cancel"){pending="";return}
        const key=pending, old=chosen
        if(!key || !old || pendingSituation)return
        const next=Bindings.move(doc,baseEdit?"":scene,selectedSpec,key,choice)
        if(settings.saveCopy(copyName.text,next)){
            selectedSpec=key+old.spec.substring(old.key.length);pending="";baseEdit=false;dest.currentIndex=-1
        }
    }
    component Caption: Label { color: Theme.t1; font.family: Theme.fontUi; wrapMode: Text.Wrap }
    component EditorField: TextField {
        implicitHeight: 34
        leftPadding: 10; rightPadding: 10
        selectByMouse: true
        color: Theme.t1
        placeholderTextColor: Theme.t2
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentInk
        background: Rectangle {
            radius: 7; color: Theme.card2
            border.color: parent.activeFocus ? Theme.accent : Theme.stroke2
        }
    }
    FileDialog { id:importFile;objectName:"import-dialog";nameFilters:["JSON (*.json)"];onAccepted:{if(page.settings.importProfile(selectedFile,""))page.resetView()} }
    FileDialog { id:exportFile;objectName:"export-dialog";fileMode:FileDialog.SaveFile;defaultSuffix:"json";nameFilters:["JSON (*.json)"];onAccepted:page.settings.exportProfile(selectedFile) }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: page.compact ? 14 : 24
            Layout.rightMargin: Layout.leftMargin
            Layout.topMargin: 14
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                VIcon { name: "keyboard"; size: 24; color: Theme.accent }
                Caption { text: qsTr("键盘与鼠标"); font.pixelSize: 21; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Ps5EditorPicker {
                    id: profilePicker
                    objectName: "profile-picker"
                    Accessible.name: qsTr("控制配置")
                    Layout.preferredWidth: Math.min(240, page.width * 0.4)
                    model: page.settings.profiles
                    currentIndex: model.indexOf(page.settings.selected)
                    onActivated: {
                        if (page.settings.choose(currentText)) page.resetView()
                        currentIndex = Qt.binding(() => model.indexOf(page.settings.selected))
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 2
                Repeater {
                    model: page.sections
                    Ps5EditorButton {
                        required property string modelData
                        required property int index
                        objectName: "section-" + index
                        text: modelData
                        quiet: true
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: checked
                        background: Item {
                            Rectangle {
                                anchors.fill: parent; radius: 6
                                color: parent.parent.down ? Theme.accentSoft : parent.parent.hovered ? Theme.card3 : "transparent"
                                border.color: parent.parent.visualFocus ? Theme.accent : "transparent"
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom; height: 2
                                visible: parent.parent.checked; color: Theme.accent
                            }
                        }
                        checked: page.section === index
                        onClicked: page.section = index
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth:true; Layout.topMargin:10; implicitHeight:1; color:Theme.stroke2 }
        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight + 28
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: body
                x: page.compact ? 14 : 24
                y: 14
                width: scroll.width - 2 * x
                spacing: 10
                ColumnLayout {
                    visible: page.section === 0
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth:true
                        Ps5EditorPicker { id:preview;objectName:"preview";Accessible.name:qsTr("当前情境");model:page.doc.contexts || [];currentIndex:model.indexOf(page.scene)
                            displayText:Bindings.situationName(page.doc,currentText)
                            delegate: ItemDelegate { required property string modelData; width:preview.width; text:Bindings.situationName(page.doc,modelData) }
                            onActivated: {
                                page.scene = currentText
                                if (page.viewMode === 1 && page.filterControl) page.selectControl(page.filterControl)
                                else page.selectInput("KeyF")
                            }
                        }
                        Item { Layout.fillWidth:true }
                        Repeater {
                            model:[{mode:2,title:qsTr("动作列表")}, {mode:0,title:qsTr("键鼠布局")}, {mode:1,title:qsTr("手柄布局")}]
                            Ps5EditorButton { quiet:true; required property var modelData;readonly property int mode: modelData.mode;text:modelData.title;checked:page.viewMode===mode;checkable:true
                                objectName:"view-"+mode
                                onClicked:{
                                    page.viewMode=mode
                                    page.filterControl=(mode===1 && page.chosen) ? (page.chosen.controls[0] || "") : ""
                                    page.pending=""
                                    if (mode === 2) {
                                        if (page.Window.window) page.Window.window.requestActivate()
                                        Qt.callLater(function() { if (search.visible) search.forceActiveFocus(Qt.TabFocusReason) })
                                    }
                                } }
                        }
                    }
                    Ps5InputDiagram {
                        visible: page.viewMode === 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.topMargin: 4
                        profile: page.doc
                        rows: page.rows
                        selectedSpec: page.selectedSpec
                        onInputSelected: key => page.selectInput(key)
                        onInputFocused: key => {
                            const y = key.mapToItem(scroll.contentItem, 0, 0).y
                            if (y < scroll.contentY) scroll.contentY = Math.max(0, y - 4)
                            else if (y + key.height > scroll.contentY + scroll.height)
                                scroll.contentY = Math.min(scroll.contentHeight - scroll.height, y + key.height + 4 - scroll.height)
                        }
                    }
                    Ps5ControllerDiagram {
                        visible: page.viewMode === 1
                        Layout.fillWidth: true
                        rows: page.rows
                        viewportHeight: scroll.height
                        selectedControl: page.filterControl
                        unassigned: qsTr("未分配")
                        onControlSelected: control => page.selectControl(control)
                    }
                    EditorField {
                        id:search;objectName:"search"; Accessible.name:placeholderText;Layout.fillWidth:true;visible:page.viewMode===2
                        placeholderText:qsTr("查找输入、游戏动作或手柄输出")
                    }
                    RowLayout {
                        visible: page.viewMode === 2
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 12
                        Caption { Layout.fillWidth: true; text: qsTr("游戏动作"); color: Theme.t2 }
                        Caption { Layout.preferredWidth: page.compact ? 84 : 138; text: qsTr("你的输入"); color: Theme.t2 }
                        Caption { Layout.preferredWidth: page.compact ? 92 : 144; text: qsTr("PS5 手柄输出"); color: Theme.t2 }
                    }
                    ColumnLayout {
                        visible:page.viewMode!==0;Layout.fillWidth:true;spacing:3
                        Caption {
                            visible: matches.count === 0
                            text: qsTr("没有匹配的绑定")
                            color: Theme.t2
                        }
                        Repeater {
                            id: matches
                            model:page.viewMode===0?[]:page.rows.filter(r=>page.viewMode===1 ? r.controls.includes(page.filterControl) :
                                    [r.input,r.title,r.output].join(" ").toLowerCase().includes(search.text.toLowerCase()))
                            Ps5EditorButton {
                                id: resultRow
                                leading: true
                                quiet: true
                                required property var modelData
                                objectName:"binding-"+modelData.spec
                                text:[modelData.input,modelData.title,modelData.output].filter(Boolean).join("   ·   ")
                                Accessible.name: text
                                contentItem: RowLayout {
                                    spacing: 12
                                    Caption { Layout.fillWidth:true; text:resultRow.modelData.shortTitle; font.weight: Font.DemiBold }
                                    Caption { Layout.preferredWidth: page.compact ? 84 : 138; text:resultRow.modelData.input; color: Theme.t2 }
                                    Caption { Layout.preferredWidth:page.compact ? 92 : 144; text:resultRow.modelData.output; color:Theme.accent }
                                }
                                checked:page.selectedSpec===modelData.spec;Layout.fillWidth:true
                                onClicked:{page.selectedSpec=modelData.spec;page.pending="";page.baseEdit=false}
                            }
                        }
                    }
                }
                Ps5MouseAim {
                    visible: page.section === 1
                    Layout.fillWidth: true
                    settings: page.settings
                    copyName: copyName.text
                }
                ColumnLayout {
                    id: situationsPanel
                    visible: page.section === 2
                    Layout.fillWidth: true
                    spacing: 0
                    readonly property var inputs: visible ? Bindings.situationInputs(page.doc) : ({})
                    Caption {
                        Layout.fillWidth: true
                        text: qsTr("情境检测")
                        font.pixelSize: Theme.fsH2
                        font.weight: Font.DemiBold
                        font.variableAxes: Theme.axesDemiBold
                    }
                    Caption {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        text: qsTr("游戏中可在播放器里手动选择情境，也可使用下列快捷键。")
                        color: Theme.t2
                    }
                    component SituationLine: Rectangle {
                        id: line
                        property string sceneId: ""
                        property bool heading: false
                        readonly property var sources: heading ? [] : Bindings.detectionSources(page.doc, sceneId)
                        readonly property var inputs: situationsPanel.inputs[sceneId] || []
                        readonly property string hotkey: (page.doc.context_hotkeys || {})[sceneId] || ""
                        Layout.fillWidth: true
                        implicitHeight: Math.max(heading ? 34 : 54, cells.implicitHeight + (heading ? 12 : 18))
                        color: heading ? Theme.card3 : "transparent"
                        radius: heading ? 6 : 0
                        RowLayout {
                            id: cells
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: page.compact ? 8 : 12
                            spacing: page.compact ? 8 : 20
                            ColumnLayout {
                                Layout.minimumWidth: 0
                                Layout.fillWidth: true
                                Layout.preferredWidth: 2
                                spacing: 2
                                Caption {
                                    Layout.fillWidth: true
                                    text: line.heading ? qsTr("情境") : Bindings.situationName(page.doc, line.sceneId)
                                    font.pixelSize: line.heading ? Theme.fsSmall : Theme.fsBody
                                    font.weight: line.heading ? Font.Medium : Font.DemiBold
                                    font.variableAxes: line.heading ? Theme.axesMedium : Theme.axesDemiBold
                                    color: line.heading ? Theme.t2 : Theme.t1
                                }
                                Caption {
                                    Layout.fillWidth: true
                                    visible: !line.heading && !line.sources.length && !line.inputs.length && !line.hotkey
                                    text: qsTr("手动选择")
                                    font.pixelSize: Theme.fsSmall
                                    color: Theme.t2
                                }
                            }
                            RowLayout {
                                Layout.minimumWidth: 0
                                Layout.fillWidth: true
                                Layout.preferredWidth: 2.5
                                spacing: 6
                                VIcon {
                                    visible: !line.heading && line.sources.length > 0
                                    name: "activity"
                                    size: 15
                                    color: Theme.exp
                                }
                                Caption {
                                    Layout.minimumWidth: 0
                                    Layout.fillWidth: true
                                    text: line.heading ? qsTr("自动检测") : line.sources.join(" · ") || "—"
                                    color: line.heading || !line.sources.length ? Theme.t2 : Theme.t1
                                    font.pixelSize: line.heading ? Theme.fsSmall : Theme.fsBody
                                }
                            }
                            Caption {
                                Layout.minimumWidth: 0
                                Layout.fillWidth: true
                                Layout.preferredWidth: 4
                                text: line.heading ? qsTr("游戏输入") : line.inputs.join(" · ") || "—"
                                color: line.heading || !line.inputs.length ? Theme.t2 : Theme.t1
                                font.pixelSize: line.heading ? Theme.fsSmall : Theme.fsBody
                            }
                            Item {
                                Layout.minimumWidth: 0
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1.6
                                implicitHeight: shortcut.implicitHeight
                                Caption {
                                    id: shortcut
                                    width: Math.min(implicitWidth, parent.width)
                                    text: line.heading ? qsTr("快捷键") : Bindings.shortcutName(line.hotkey) || "—"
                                    readonly property bool isKey: !line.heading && !!line.hotkey
                                    leftPadding: isKey ? 8 : 0
                                    rightPadding: leftPadding
                                    topPadding: isKey ? 3 : 0
                                    bottomPadding: topPadding
                                    color: isKey ? Theme.t1 : Theme.t2
                                    font.family: isKey ? Theme.fontMono : Theme.fontUi
                                    font.pixelSize: line.heading ? Theme.fsSmall : Theme.fsBody
                                    background: Rectangle {
                                        visible: shortcut.isKey
                                        color: Theme.card2
                                        border.color: Theme.stroke2
                                        radius: 4
                                    }
                                }
                            }
                        }
                        Rectangle {
                            visible: !line.heading
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.stroke
                        }
                    }
                    SituationLine { heading: true; Layout.topMargin: 16 }
                    Repeater {
                        model: situationsPanel.visible && page.settings ? page.settings.situations : []
                        SituationLine {
                            required property string modelData
                            sceneId: modelData
                            objectName: "situation-" + sceneId
                        }
                    }
                }
                ColumnLayout {
                    visible: page.section === 3
                    Layout.fillWidth: true
                    Layout.maximumWidth: 610
                    spacing: 20
                    Ps5ShortcutSettings {
                        id: shortcutSettings
                        Layout.fillWidth: true
                        settings: page.settings
                        onResolved: page.resetView()
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke2 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Caption { text: qsTr("本机输入预览"); font.pixelSize: 17; font.weight: Font.DemiBold }
                            Caption { Layout.fillWidth: true; text: qsTr("在预览中移动鼠标并按 W。输入仅在本机生效。"); color: Theme.t2 }
                        }
                        Ps5EditorButton { objectName: "try-controls"; text: qsTr("试用控制"); onClicked: page.practiceRequested() }
                    }
                }
                ColumnLayout {
                    visible: page.section === 4
                    Layout.fillWidth: true
                    spacing: 12
                    Caption { text: qsTr("个人副本名称"); font.weight: Font.DemiBold; font.pixelSize:16 }
                    RowLayout {
                        Layout.fillWidth: true
                        EditorField {
                            id: copyName; objectName: "copy-name"
                            Layout.fillWidth:true; Layout.maximumWidth:360
                            Accessible.name: qsTr("个人副本名称")
                            placeholderText: qsTr("个人副本名称")
                        }
                        Ps5EditorButton { text:qsTr("保存副本"); onClicked:page.settings.saveCopy(copyName.text,page.doc) }
                    }
                    RowLayout {
                        Ps5EditorButton { objectName:"import-button"; text:qsTr("导入"); onClicked:importFile.open() }
                        Ps5EditorButton { objectName:"export-button"; text:qsTr("导出"); onClicked:exportFile.open() }
                    }
                }
                ColumnLayout {
                    visible: page.section === 4
                    Layout.fillWidth: true
                    Layout.maximumWidth: 750
                    Layout.topMargin: 14
                    spacing: 24
                    Repeater {
                        model:["required","recommended"]
                        ColumnLayout {
                            id: settingGroup
                            required property string modelData
                            readonly property var items: (page.doc.required_game_settings || []).filter(s =>
                                (s.importance === "required") === (modelData === "required"))
                            visible: items.length > 0
                            Layout.fillWidth:true
                            spacing:0
                            Caption {
                                text:settingGroup.modelData === "required" ? qsTr("必需") : qsTr("推荐")
                                font.pixelSize:17; font.weight:Font.DemiBold; Layout.bottomMargin:8
                            }
                            Repeater {
                                model: settingGroup.items
                                ColumnLayout {
                                    id: settingRow
                                    required property var modelData
                                    objectName:"setting-"+modelData.id
                                    readonly property string label:Bindings.label(modelData.label,modelData.id)
                                    readonly property string path:Bindings.label(modelData.path_label,modelData.path)
                                    readonly property string value:Bindings.label(modelData.value_label,modelData.value)
                                    Layout.fillWidth:true; spacing:0
                                    Rectangle { Layout.fillWidth:true; implicitHeight:1; color:Theme.stroke }
                                    RowLayout {
                                        Layout.fillWidth:true; Layout.topMargin:8; Layout.bottomMargin:8; spacing:16
                                        ColumnLayout {
                                            Layout.fillWidth:true; spacing:1
                                            Caption { Layout.fillWidth:true; text:settingRow.label; font.weight:Font.DemiBold }
                                            Caption { Layout.fillWidth:true; text:settingRow.path; color:Theme.t2; font.pixelSize:11 }
                                        }
                                        Caption { Layout.preferredWidth:Math.min(260,page.width * 0.29); text:settingRow.value }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: page.section === 0
            Layout.fillWidth: true
            implicitHeight: inspector.implicitHeight + 20
            color: Theme.card2
            ColumnLayout {
                id: inspector
                anchors.verticalCenter: parent.verticalCenter
                x: page.compact ? 14 : 24
                width: parent.width - 2 * x
                spacing: 5
                Caption { text: qsTr("已选输入"); color:Theme.t2; font.pixelSize:12 }
                Caption {
                    objectName:"selected-binding";Layout.fillWidth:true; font.pixelSize:page.compact ? 15 : 18; font.weight:Font.DemiBold
                    text:page.chosen ? [page.chosen.input,page.chosen.shortTitle,page.chosen.output].filter(Boolean).join("   →   ")
                                     : page.selectedShortcut ? [Bindings.caption(page.selectedSpec),
                                                               Bindings.situationName(page.doc, page.selectedShortcut)].join("   →   ")
                                     : [page.filterControl ? Bindings.controllerName(page.filterControl) : Bindings.caption(page.selectedSpec),
                                        qsTr("未分配")].filter(Boolean).join(" · ")
                }
                Flow {
                    visible: page.viewMode === 0 && page.selectedGestures.length > 1
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: page.selectedGestures
                        Ps5EditorButton {
                            required property var modelData
                            objectName: "gesture-" + modelData.spec
                            quiet: true
                            text: Bindings.gestureName(page.doc, modelData.spec)
                            checked: page.selectedSpec === modelData.spec
                            onClicked: {
                                page.listening = false
                                page.pending = ""
                                page.baseEdit = false
                                page.selectedSpec = modelData.spec
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    visible:!!page.chosen && !page.chosen.fixed
                    CheckBox {
                        height: 34
                        text:qsTr("更改基础绑定");objectName:"base-edit";checked:page.baseEdit
                        enabled:!!page.chosen && page.doc.bindings[page.selectedSpec]===page.chosen.action
                        onToggled:{page.baseEdit=checked;page.pending=""}
                    }
                    Caption { height: 34; verticalAlignment: Text.AlignVCenter; text:page.baseEdit?qsTr("基础绑定"):Bindings.situationName(page.doc,page.scene) }
                    Ps5EditorButton {
                        id: listen
                        objectName: "listen-binding"
                        text: page.listening ? qsTr("请按键…") : qsTr("更改按键")
                        primary: true
                        onClicked: { page.listening = true; forceActiveFocus() }
                        onActiveFocusChanged: if (!activeFocus) page.listening = false
                        Keys.onPressed: event => {
                            if (!page.listening) return
                            event.accepted = true
                            if (event.isAutoRepeat) return
                            if (event.key === Qt.Key_Escape) { page.listening = false; return }
                            const code = page.settings.keyCode(event.key, event.nativeScanCode)
                            if (code) { page.listening = false; page.destination(code) }
                        }
                    }
                    Ps5EditorPicker { id:dest;objectName:"destination";Accessible.name:qsTr("移至此输入");model:page.destinations;currentIndex:-1;displayText:currentIndex < 0 ? qsTr("选择目标输入") : Bindings.caption(currentText);width:Math.max(150, implicitWidth) }
                    Ps5EditorButton { objectName:"move";text:qsTr("移至此输入");enabled:dest.currentIndex >= 0 && !!page.chosen && dest.currentText !== page.chosen.key;onClicked:page.destination(dest.currentText) }
                }
                ColumnLayout {
                    visible:page.pending!=="";Layout.fillWidth:true
                    Caption { objectName:"affected-situations";visible:page.baseEdit;text:qsTr("受影响的情境：%1").arg(page.chosen?Bindings.changesIn(page.doc,page.chosen.key,page.pending).map(c=>Bindings.situationName(page.doc,c)).join(", "):"") }
                    Caption {
                        objectName: "conflict"
                        Layout.fillWidth: true
                        readonly property string occupiedBy: page.pendingSituation
                            ? qsTr("切换情境 · %1").arg(Bindings.situationName(page.doc, page.pendingSituation))
                            : page.occupied.map(row => row.title).join(", ")
                        text: page.chosen && occupiedBy ? qsTr("%1 与 %2 在 %3 上冲突").arg(page.chosen.title)
                              .arg(occupiedBy).arg(Bindings.caption(page.pending)) : ""
                    }
                    RowLayout {
                        Ps5EditorButton { objectName:"swap";visible:!page.pendingSituation && page.occupied.length>0;text:qsTr("交换");onClicked:page.applyMove("swap") }
                        Ps5EditorButton { objectName:"replace";visible:!page.pendingSituation;text:page.occupied.length?qsTr("替换"):qsTr("应用");onClicked:page.applyMove("replace") }
                        Ps5EditorButton { objectName:"cancel-move";text:qsTr("取消");onClicked:page.applyMove("cancel") }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke2 }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: page.compact ? 14 : 24
            Layout.rightMargin: Layout.leftMargin
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            Caption { objectName:"dialog-foot"; text:page.settings.error; color:Theme.err; Layout.fillWidth:true }
            Ps5EditorButton { id:done; objectName:"done"; text:qsTr("完成"); primary:!page.settings.available; onClicked:page.dismissRequested(false) }
            Ps5EditorButton { objectName:"resume"; visible:page.settings.available; primary:true; text:qsTr("恢复捕获输入"); onClicked:page.dismissRequested(true) }
        }
    }
}
