// G1.6 component smoke tests: each control is driven through real mouse events and
// its visible state checked, so a control that draws but does nothing fails here.
import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

Item {
    id: root
    width: 1480
    height: 900

    VSwitch { id: sw; x: 10; y: 10 }
    SignalSpy { id: swSpy; target: sw; signalName: "toggled" }

    VSeg {
        id: seg
        x: 10; y: 50
        options: [{ id: "a", label: "2K" }, { id: "b", label: "4K" }, { id: "c", label: "8K" }]
        current: "a"
        onPicked: id => current = id
    }

    VSlider { id: slider; x: 10; y: 100; width: 200; from: 0; to: 100; value: 50 }
    SignalSpy { id: sliderSpy; target: slider; signalName: "moved" }
    VButton { id: underSlider; x: 680; y: 100; width: 200; height: 20; text: "underlying inspector" }
    SignalSpy { id: underSliderSpy; target: underSlider; signalName: "clicked" }
    VSlider { id: overSlider; x: 680; y: 100; width: 200; from: 0; to: 100; value: 50 }
    VSlider { id: resetSlider; x: 10; y: 135; width: 200; from: 0; to: 100; value: 25; resettable: true; defaultValue: 50 }
    SignalSpy { id: resetSpy; target: resetSlider; signalName: "moved" }

    QtObject { id: boundState; property real amount: 25; property bool acceptEdits: true }
    VSlider {
        id: boundSlider; x: 240; y: 135; width: 200
        from: 0; to: 100; value: boundState.amount
        valueFromModel: true
        resettable: true; defaultValue: 50
        onMoved: value => { if (boundState.acceptEdits) boundState.amount = value }
    }

    VAccordion {
        id: acc
        x: 300; y: 10; width: 320
        title: "超分辨率"
        summary: "测试"
        glyph: "sparkles"
        enabledSwitch: false
        Rectangle { implicitWidth: 100; implicitHeight: 120; color: "red" }
    }

    Item { id: anchorItem; x: 300; y: 400; width: 120; height: 30 }
    VMenu {
        id: menu
        title: "测试"
        items: [{ label: "甲", checked: true }, { sep: true }, { label: "乙" }, { label: "丙", disabled: true }]
    }
    VMenu {
        id: dynamicMenu
        title: "动态菜单"
        aboveLimit: 360
        items: [{ label: "甲" }]
    }
    SignalSpy { id: menuSpy; target: menu; signalName: "picked" }

    // Real controls with an explicit snapshot fixture; this is not an engine test.
    Component {
        id: nrFixture
        Item {
            id: fixture
            width: 1480; height: 900; z: 20
            property int lastIndex: -1
            property string lastKey: ""
            property var rows: [0,1,2,3].map(i => ({
                index: 2*i+1, enabled: true, runtime: 0, sizePolicy: 0, intensity: 1,
                tone: 1, structure: 1, skin: -1, style: 0, autoMask: false,
                uiCorrection: false, total: 1, darken: 1, brighten: 1,
                color: 1, luminance: 1, temporal: false
            }))
            function edit(nodeIndex,key,amount) {
                lastIndex=nodeIndex; lastKey=key
                const next=rows.map(r => Object.assign({},r))
                next.find(r => r.index===nodeIndex)[key]=amount
                rows=next
            }
            Repeater {
                model: 4
                delegate: ProPage.NrLayerCard {
                    required property int index
                    x: index*370; width: 360
                    layerData: fixture.rows[index]
                    layerNumber: index+1; layerCount: 4
                    onEdited: (nodeIndex,key,amount) => fixture.edit(nodeIndex,key,amount)
                    onEnabledEdited: (nodeIndex,enabled) => fixture.edit(nodeIndex,"enabled",enabled)
                }
            }
        }
    }

    Component {
        id: colourFixture
        Item {
            id: fixture
            width:1100;height:800;z:40
            property int edits:0
            property string lastKey:""
            ColourCurve {
                id: curve;objectName:"test-curve";x:10;y:10;width:300
                onEdited:(channel,points)=>{
                    const copy=curves.slice();copy[channel]=points;curves=copy;++fixture.edits
                }
            }
            ColourMixer {
                id:mixer;objectName:"test-mixer";x:330;y:10;width:320
                onModeEdited:blackWhite=>state=Object.assign({},state,{blackWhite:blackWhite})
                onEdited:(name,value)=>{
                    const parts=name.split("."),next=Object.assign({},state),arr=state[parts[0]].slice()
                    arr[Number(parts[1])]=value;next[parts[0]]=arr;state=next;fixture.lastKey=name;++fixture.edits
                }
            }
            Row {
                x:10;y:400;spacing:20
                Repeater {
                    model:4
                    ColourWheel {
                        property bool sliderResetEnabled: true
                        property bool sliderValueFromModel: true
                        required property int index
                        objectName:"test-wheel-"+index;width:150
                        title:["阴影","中间调","高光","全局"][index]
                        onEdited:(h,s,l)=>{hue=h;saturation=s;luminance=l;++fixture.edits}
                    }
                }
            }
        }
    }

    TestCase {
        name: "Components"
        when: windowShown

        function initTestCase() { Theme.reduced = true }   // no waiting on transitions

        function test_colour_mixer_design_motion() {
            const f=createTemporaryObject(colourFixture,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            const mixer=findChild(f,"test-mixer")
            const band=i=>findChild(mixer,"colour-mixer-band-"+i)
            compare(band(5).height,22);compare(band(5).radius,7)
            compare(band(0).scale,1.08);compare(band(5).scale,1)
            try {
                Theme.reduced=false
                const start=Date.now(),samples=[]
                mouseClick(band(5),band(5).width/2,band(5).height/2)
                for(let i=0;i<32;++i){
                    samples.push({ms:Date.now()-start,scale:band(5).scale})
                    wait(16)
                }
                console.log("P3_MIXER_MOTION "+JSON.stringify(samples))
                verify(samples.some(s=>s.scale>1&&s.scale<1.08),"real intermediate frames")
                verify(samples.some(s=>s.scale>1.08),"design spring overshoot")
                fuzzyCompare(band(5).scale,1.08,0.0001)
                for(const i of [2,4,7]){mouseClick(band(i),band(i).width/2,band(i).height/2);wait(48)}
                wait(420);compare(mixer.band,7)
                for(let i=0;i<8;++i)fuzzyCompare(band(i).scale,i===7?1.08:1,0.0001)
                Theme.reduced=true
                mouseClick(band(3),band(3).width/2,band(3).height/2)
                compare(mixer.band,3);fuzzyCompare(band(3).scale,1.08,0.0001)
                fuzzyCompare(band(7).scale,1,0.0001)
            } finally { Theme.reduced=true }
        }

        function test_accordion_header_action_keeps_open_state() {
            const f=createTemporaryQmlObject(`import QtQuick; import Veyra; VAccordion {
                id:fixture;x:700;y:10;width:330;open:true;enabledSwitch:false;title:"颜色";
                property int resets:0;
                headerActions: [VButton {objectName:"test-header-reset";text:"还原";implicitWidth:40;implicitHeight:24;onClicked:fixture.resets++}]
                Rectangle {implicitHeight:80;implicitWidth:100}
            }`,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            const button=findChild(f,"test-header-reset")
            mouseClick(button,button.width/2,button.height/2)
            compare(f.resets,1);verify(f.open,"header action must not collapse section")
            mouseClick(f,80,24);verify(!f.open,"ordinary header still toggles")
        }

        function test_colour_header_leading_action() {
            const f=createTemporaryQmlObject(`import QtQuick; import Veyra; VAccordion {
                id:fixture;x:700;y:10;width:330;open:true;enabledSwitch:false;title:"颜色";
                compactHeader:true;property int resets:0;
                headerLeadingActions: [VButton {objectName:"test-header-eye";icon:true;iconName:fixture.bypassed?"eyeoff":"eye";ghost:true;implicitWidth:24;implicitHeight:24;onClicked:fixture.bypassed=!fixture.bypassed}]
                headerActions: [VButton {objectName:"test-header-reset";icon:true;iconName:"reset";ghost:true;implicitWidth:26;implicitHeight:26;onClicked:fixture.resets++}]
                Rectangle {objectName:"test-header-body";implicitHeight:80;implicitWidth:100}
            }`,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            compare(f.headerHeight,42);compare(acc.headerHeight,48)
            const eye=findChild(f,"test-header-eye"),reset=findChild(f,"test-header-reset")
            const body=findChild(f,"test-header-body").parent
            verify(eye.mapToItem(f,0,0).x<reset.mapToItem(f,0,0).x)
            mouseClick(eye,12,12);verify(f.bypassed);verify(f.open);compare(eye.iconName,"eyeoff")
            tryCompare(body,"opacity",0.45)
            mouseClick(reset,13,13);compare(f.resets,1);verify(f.open);verify(f.bypassed)
            mouseClick(eye,12,12);verify(!f.bypassed);verify(f.open);tryCompare(body,"opacity",1)
            mouseClick(f,80,21);verify(!f.open);tryCompare(body,"opacity",0)
        }

        function test_switch() {
            sw.checked=false
            swSpy.clear()
            compare(sw.checked, false)
            mouseClick(sw)
            compare(sw.checked, true)
            compare(swSpy.count, 1)
            compare(swSpy.signalArguments[0][0], true)
            mouseClick(sw)
            compare(sw.checked, false)
        }

        function test_seg() {
            compare(seg.currentIndex, 0)
            // Click the third label; the indicator follows it.
            const third = seg.currentItem.parent.children[2]
            mouseClick(third)
            compare(seg.current, "c")
            compare(seg.currentIndex, 2)
            tryCompare(seg, "indicatorX", seg.targetX)
        }

        function test_slider_tap() {
            sliderSpy.clear()
            mouseClick(slider, 150, slider.height / 2)
            fuzzyCompare(slider.value, 75, 1)
            compare(sliderSpy.count, 1)
        }

        function test_slider_does_not_click_through() {
            underSliderSpy.clear()
            overSlider.value = 50
            mouseClick(overSlider, 150, overSlider.height / 2)
            fuzzyCompare(overSlider.value, 75, 1)
            compare(underSliderSpy.count, 0, "Foreground slider must not open an underlying node inspector")
        }

        function test_slider_reset_nonzero_and_hit_area() {
            resetSlider.enabledControl = true
            resetSlider.value = 25
            resetSpy.clear()
            const button = findChild(resetSlider, "vslider-reset")
            verify(button !== null && button.visible)
            mouseClick(button, button.width / 2, button.height / 2)
            compare(resetSlider.value, 50)
            compare(resetSpy.count, 1)
            compare(resetSpy.signalArguments[0][0], 50)
            // The reset click must not also jump to the track's right edge.
            mouseClick(button, button.width / 2, button.height / 2)
            compare(resetSpy.count, 1)
            mouseClick(resetSlider, 43, resetSlider.height / 2)
            fuzzyCompare(resetSlider.value, 25, 1)
            compare(resetSpy.count, 2)
            resetSlider.enabledControl = false
            mouseClick(button, button.width / 2, button.height / 2)
            mouseClick(resetSlider, 120, resetSlider.height / 2)
            fuzzyCompare(resetSlider.value, 25, 1)
            compare(resetSpy.count, 2)
            resetSlider.enabledControl = true
        }

        function test_slider_reset_exact_endpoint_and_sentinel() {
            const button = findChild(resetSlider, "vslider-reset")
            for (const amount of [0, 100, -1]) {
                resetSlider.value = 25
                resetSlider.defaultValue = amount
                resetSpy.clear()
                mouseClick(button, button.width / 2, button.height / 2)
                compare(resetSlider.value, amount)
                compare(resetSpy.count, 1)
            }
            resetSlider.defaultValue = 50
            compare(findChild(slider, "vslider-reset").visible, false)
        }

        function test_slider_keeps_external_parameter_binding() {
            boundState.acceptEdits = true
            boundState.amount = 25
            mouseClick(boundSlider, boundSlider.trackWidth * 0.75, boundSlider.height / 2)
            fuzzyCompare(boundState.amount, 75, 1)
            boundState.amount = 35
            compare(boundSlider.value, 35, "External group/node reset must update an edited slider")
            const button = findChild(boundSlider, "vslider-reset")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(boundState.amount, 50)
            boundState.amount = 15
            compare(boundSlider.value, 15, "Single-parameter reset must preserve model binding")
            boundState.amount = 50
            const x = boundSlider.trackWidth / 2, y = boundSlider.height / 2
            mousePress(boundSlider, x, y)
            for (let i = 1; i <= 8; ++i) mouseMove(boundSlider, x + i * 5, y)
            mouseRelease(boundSlider, x + 40, y)
            fuzzyCompare(boundState.amount, 50 + 4000 / boundSlider.trackWidth, 2)
            boundState.amount = 20
            compare(boundSlider.value, 20, "Dragging must preserve model binding")
            boundState.acceptEdits = false
            mouseClick(boundSlider, boundSlider.trackWidth * 0.75, y)
            compare(boundSlider.value, 20, "Rejected backend edit must not appear accepted")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(boundSlider.value, 20, "Rejected reset must not override the model")
            boundState.acceptEdits = true
        }

        function test_slider_drag() {
            // Drag from the knob 40 px right: the value moves by 40/200 of the range,
            // not by a running total (the G1.5 drag fix).
            slider.value = 50
            sliderSpy.clear()
            const y = slider.height / 2
            mousePress(slider, 100, y)
            for (let i = 1; i <= 8; ++i) mouseMove(slider, 100 + i * 5, y)
            mouseRelease(slider, 140, y)
            fuzzyCompare(slider.value, 70, 2)
            compare(sliderSpy.count, 1)
        }

        function test_accordion() {
            compare(acc.open, false)
            compare(acc.implicitHeight, acc.headerHeight)
            mouseClick(acc, 40, acc.headerHeight / 2)
            compare(acc.open, true)
            verify(acc.implicitHeight >= acc.headerHeight + 120)
            mouseClick(acc, 40, acc.headerHeight / 2)
            compare(acc.open, false)
        }

        function test_menu() {
            menu.openAt(anchorItem, "up")
            tryCompare(menu, "opened", true)
            // Opens above the anchor, from its centre.
            verify(menu.y + menu.height <= anchorItem.y)
            compare(menu.checks[0], true)
            // Pick 乙 (index 2): the tick moves at once, the signal comes 130 ms later.
            const rows = findRows(menu.contentItem)
            compare(rows.length, 3)
            mouseClick(rows[1])
            compare(menu.checks[2], true)
            compare(menu.checks[0], false)
            tryCompare(menuSpy, "count", 1)
            compare(menuSpy.signalArguments[0][0], 2)
            tryCompare(menu, "visible", false)
            // A disabled row does nothing.
            menu.openAt(anchorItem, "up")
            tryCompare(menu, "opened", true)
            mouseClick(findRows(menu.contentItem)[2])
            wait(200)
            compare(menuSpy.count, 1)
            menu.close()
        }

        function test_menu_repositions_after_content_growth() {
            dynamicMenu.openAt(anchorItem, "up")
            tryCompare(dynamicMenu, "opened", true)
            const initialHeight = dynamicMenu.height
            dynamicMenu.items = [{ label: "甲", note: "第一项" },
                                 { label: "乙", note: "第二项" },
                                 { label: "丙", note: "第三项" }]
            tryVerify(() => dynamicMenu.height > initialHeight)
            tryVerify(() => dynamicMenu.y + dynamicMenu.height <= dynamicMenu.aboveLimit)
            dynamicMenu.close()
        }

        function popupFor(select) {
            for (const c of select.data)
                if (c && typeof c.openAt === "function") return c
            fail("select popup not found")
        }

        function pickOption(select,index) {
            verify(select.width>20 && select.height>10)
            const position=select.mapToItem(root,0,0)
            console.log("SELECT_REAL_HIT",JSON.stringify({name:select.objectName,index:index,x:position.x,y:position.y,w:select.width,h:select.height,enabled:select.enabled,visible:select.visible,windowWidth:root.Window.window.width,windowHeight:root.Window.window.height}))
            verify(waitForPolish(root.Window.window))
            wait(550) // finish the real accordion's opening/clipping transition
            mouseClick(select)
            const popup=popupFor(select)
            tryCompare(popup,"opened",true)
            verify(waitForPolish(root.Window.window))
            const rows=findRows(popup.contentItem)
            compare(rows.length,select.options.length)
            mouseClick(rows[index])
            tryCompare(popup,"visible",false)
            verify(waitForPolish(root.Window.window))
        }

        function test_nr_four_independent_cards() {
            const fixture=createTemporaryObject(nrFixture,root)
            verify(fixture)
            verify(waitForPolish(root.Window.window))
            const policies=[2,3,5,1]
            const optionIndices=[0,1,4,5]
            for(let i=0;i<4;++i) {
                const card=findChild(fixture,"nr-card-"+(i+1))
                // 2026-09-29 P5: the layer cards sit inside the NR stage card, titled "NR 层 N".
                verify(card.title.includes("NR 层 "+(i+1)))
                compare(card.layerData.sizePolicy,0)
                mouseClick(card,40,24)
                tryCompare(card,"open",true)
                const resolution=findChild(card,"nr-resolution")
                compare(resolution.options.slice(0,6).map(o=>o.label).join(","),"480p,720p,900p,1080p · 默认,1440p,原生")
                compare(resolution.options.length,7)
                compare(resolution.options[6].id,"6")
                compare(resolution.options[6].disabled,true)
                pickOption(resolution,optionIndices[i])
                compare(fixture.rows[i].sizePolicy,policies[i])
                const intensity=findChild(card,"nr-intensity")
                mouseClick(intensity,intensity.trackWidth*(0.2+0.15*i),intensity.height/2)
                fuzzyCompare(fixture.rows[i].intensity,0.2+0.15*i,0.015)
                compare(fixture.lastIndex,2*i+1)
                compare(fixture.lastKey,"intensity")
                verify(!findChild(card,"nr-copy").enabled)
                // Snapshot updates preserve delegate and expansion state.
                compare(findChild(fixture,"nr-card-"+(i+1)),card)
                compare(card.open,true)
            }
            for(let i=0;i<4;++i) {
                compare(fixture.rows[i].sizePolicy,policies[i])
                fuzzyCompare(fixture.rows[i].intensity,0.2+0.15*i,0.015)
            }
        }

        function test_nr_complete_parameters_and_skin_range() {
            const fixture=createTemporaryObject(nrFixture,root)
            verify(waitForPolish(root.Window.window))
            const card=findChild(fixture,"nr-card-2")
            mouseClick(card,40,24)
            tryCompare(card,"open",true)
            const model=findChild(card,"nr-model-group")
            tryVerify(()=>model.width>300)
            mouseClick(model,40,15)
            tryCompare(model,"expanded",true)
            verify(waitForPolish(root.Window.window))
            for(const key of ["tone","structure"]) {
                const control=findChild(card,"nr-"+key)
                verify(control.width>50)
                mouseClick(control,control.trackWidth/4,control.height/2)
                fuzzyCompare(fixture.rows[1][key],0.25,0.015)
                const reset=findChild(control,"vslider-reset")
                mouseClick(reset,reset.width/2,reset.height/2)
                compare(fixture.rows[1][key],1)
                compare(fixture.rows[0][key],1)
                fixture.rows[1][key] = 0.6
                fixture.rows = fixture.rows.slice()
                compare(control.value, 0.6, "NR control must follow external instance updates")
            }
            compare(fixture.rows[1].skin,-1)
            pickOption(findChild(card,"nr-skin-mode"),1)
            compare(fixture.rows[1].skin,0)
            const skin=findChild(card,"nr-skin")
            compare(skin.from,0); compare(skin.to,2)
            mouseClick(skin,skin.trackWidth/4,skin.height/2)
            fuzzyCompare(fixture.rows[1].skin,0.5,0.02)
            const skinReset=findChild(skin,"vslider-reset")
            mouseClick(skinReset,skinReset.width/2,skinReset.height/2)
            compare(fixture.rows[1].skin,-1)
            for(const key of ["autoMask","uiCorrection"]) {
                mouseClick(findChild(card,"nr-"+key))
                compare(fixture.rows[1][key],1)
            }
            const style=findChild(card,"nr-style")
            mouseClick(style.currentItem.parent.children[2])
            compare(fixture.rows[1].style,2)
            mouseClick(model,40,15)
            verify(waitForPolish(root.Window.window))
            const residual=findChild(card,"nr-residual-group")
            verify(residual.width>300)
            mouseClick(residual,40,15)
            tryCompare(residual,"expanded",true)
            verify(waitForPolish(root.Window.window))
            for(const key of ["total","darken","brighten","color","luminance"]) {
                const control=findChild(card,"nr-"+key)
                verify(control.width>50)
                mouseClick(control,control.trackWidth/4,control.height/2)
                fuzzyCompare(fixture.rows[1][key],0.5,0.02)
                const reset=findChild(control,"vslider-reset")
                mouseClick(reset,reset.width/2,reset.height/2)
                compare(fixture.rows[1][key],1)
                compare(fixture.rows[0][key],1)
                compare(fixture.lastIndex,3)
            }
            mouseClick(residual,40,15)
            verify(waitForPolish(root.Window.window))
            const experimental=findChild(card,"nr-experimental-group")
            mouseClick(experimental,40,15)
            tryCompare(experimental,"expanded",true)
            verify(waitForPolish(root.Window.window))
            verify(findChild(card,"nr-temporal").visible)
            compare(fixture.rows[0].skin,-1)
            compare(fixture.rows[0].style,0)
        }

        function test_colour_curve_mouse_edit() {
            const f=createTemporaryObject(colourFixture,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            const curve=findChild(f,"test-curve"),canvas=findChild(curve,"colour-curve-canvas")
            verify(canvas.width>200);compare(curve.points.length,2)
            mouseDoubleClickSequence(canvas,canvas.width*0.5,canvas.height*0.3)
            tryCompare(curve.points,"length",3)
            verify(Math.abs(curve.points[1].y-0.7)<0.03)
            mouseDrag(canvas,canvas.width*0.5,canvas.height*0.3,canvas.width*0.18,canvas.height*0.1)
            verify(curve.points[1].x>0.6&&curve.points[1].x<1)
            mouseClick(canvas,curve.points[1].x*canvas.width,(1-curve.points[1].y)*canvas.height,Qt.RightButton)
            compare(curve.points.length,2)
            mouseDrag(canvas,1,canvas.height-1,canvas.width*0.3,-canvas.height*0.2)
            compare(curve.points[0].x,0)
            verify(curve.points[0].y>0.1)
            curve.channel=1;compare(curve.points[0].y,0)
            verify(f.edits>2)
        }
        function test_colour_mixer_band_isolation() {
            const f=createTemporaryObject(colourFixture,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            const mixer=findChild(f,"test-mixer")
            compare(findChild(mixer,"colour-mixer-mode-row").label,"模式")
            const hueRow=findChild(mixer,"colour-mixer-row-mixerHue")
            const satRow=findChild(mixer,"colour-mixer-row-mixerSaturation")
            compare(satRow.y-hueRow.y,38)
            const band=findChild(mixer,"colour-mixer-band-5")
            mouseClick(band,band.width/2,band.height/2);compare(mixer.band,5)
            const sat=findChild(mixer,"colour-mixer-mixerSaturation")
            compare(sat.width,150)
            const hue=findChild(mixer,"colour-mixer-mixerHue")
            const luminance=findChild(mixer,"colour-mixer-mixerLuminance")
            compare(hue.mapToItem(mixer,0,0).x,sat.mapToItem(mixer,0,0).x)
            compare(luminance.mapToItem(mixer,0,0).x,sat.mapToItem(mixer,0,0).x)
            mouseClick(sat,sat.trackWidth*0.85,sat.height/2)
            compare(f.lastKey,"mixerSaturation.5")
            verify(mixer.state.mixerSaturation[5]>50);compare(mixer.state.mixerSaturation[0],0)
            const fill=findChild(sat,"vslider-fill")
            verify(fill.visible);compare(fill.x,sat.trackWidth/2);verify(fill.width>0)
            verify(findChild(mixer,"colour-mixer-value-mixerSaturation").text.startsWith("+"))
            mouseClick(sat,sat.trackWidth*0.2,sat.height/2)
            verify(fill.x<sat.trackWidth/2);verify(Math.abs(fill.x+fill.width-sat.trackWidth/2)<0.01)
            verify(mixer.state.mixerSaturation[5]<0);compare(mixer.state.mixerSaturation[0],0)
            const mode=findChild(mixer,"colour-mixer-mode")
            mouseClick(mode,mode.width-20,mode.height/2)
            verify(mixer.state.blackWhite);verify(waitForPolish(root.Window.window))
            const bw=findChild(mixer,"colour-mixer-blackWhiteMix")
            compare(bw.width,150)
            mouseClick(bw,bw.trackWidth*0.2,bw.height/2)
            compare(f.lastKey,"blackWhiteMix.5")
            verify(mixer.state.blackWhiteMix[5]<-40);compare(mixer.state.blackWhiteMix[4],0)
            const previousSaturation=mixer.state.mixerSaturation[5]
            const reset=findChild(bw,"vslider-reset")
            mouseClick(reset,reset.width/2,reset.height/2)
            compare(mixer.state.blackWhiteMix[5],0)
            compare(mixer.state.mixerSaturation[5],previousSaturation)
        }
        function test_colour_wheels_mouse_and_reset() {
            const f=createTemporaryObject(colourFixture,root)
            verify(f!==null);verify(waitForPolish(root.Window.window))
            for(let i=0;i<4;++i){
                const wheel=findChild(f,"test-wheel-"+i),disc=findChild(wheel,"colour-wheel-disc")
                mouseClick(disc,disc.width*0.5,disc.height*0.9)
                verify(Math.abs(wheel.hue-90)<2);verify(wheel.saturation>75)
                const l=findChild(wheel,"colour-wheel-luminance")
                mouseClick(l,l.trackWidth*0.75,l.height/2);verify(wheel.luminance>40)
                const previousHue=wheel.hue,previousSaturation=wheel.saturation
                const button=findChild(l,"vslider-reset")
                verify(button.visible)
                mouseClick(button,button.width/2,button.height/2)
                compare(wheel.luminance,0)
                compare(wheel.hue,previousHue);compare(wheel.saturation,previousSaturation)
                wheel.luminance = 20
                compare(l.value,20,"Wheel luminance must follow group updates after reset")
                mouseDoubleClickSequence(disc,disc.width/2,disc.height/2)
                compare(wheel.hue,0);compare(wheel.saturation,0);compare(wheel.luminance,0)
            }
        }

        // The option rows: Rectangles with radius 9 inside the menu's list.
        function findRows(item) {
            let out = []
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (c.radius === 9 && c.implicitHeight > 20) out.push(c)
                out = out.concat(findRows(c))
            }
            return out
        }
    }
}
