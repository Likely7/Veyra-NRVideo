import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 500; height: 540
    property var params: ({darken:1,brighten:1,color:1,luminance:1})
    property var edits: []
    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: pad.implicitHeight + 180
        clip: true
    NrWebPad {
        id: pad
        objectName: "test-web"
        width: 400
        identity: "layer-1"
        values: root.params
        axes: [{key:"darken",label:"Darken",min:0,max:2,defaultValue:1},
               {key:"brighten",label:"Brighten",min:0,max:2,defaultValue:1},
               {key:"color",label:"Color",min:0,max:2,defaultValue:1},
               {key:"luminance",label:"Luminance",min:0,max:2,defaultValue:1}]
        onEdited: (key,value) => {
            const next = Object.assign({},root.params);next[key]=value;root.params=next
            root.edits=root.edits.concat([{key:key,value:value,identity:pad.identity}])
        }
    }
    }
    TestCase {
        name: "NrWebPad"
        when: windowShown
        function initTestCase() { wait(100); verify(waitForPolish(root.Window.window)) }
        function init() {
            scroller.cancelFlick();scroller.contentY=0
            pad.enabled=true;pad.editable=true;pad.identity="layer-1";pad.selectedAxis=0
            root.params={darken:1,brighten:1,color:1,luminance:1};root.edits=[]
        }
        function surface(){return findChild(pad,"test-web-surface")}
        function test_radial_drag_only_changes_selected_axis() {
            const s=surface(),p=pad.pointAt(1,0.5),q=pad.pointAt(1,0.8)
            mousePress(s,p.x,p.y);mouseMove(s,q.x,q.y);wait(70);mouseRelease(s,q.x,q.y)
            fuzzyCompare(root.params.brighten,1.6,0.03)
            compare(root.params.darken,1);compare(root.params.color,1);compare(root.params.luminance,1)
            compare(pad.dragging,false)
            compare(scroller.contentY,0)
        }
        function test_outside_axes_can_scroll_the_parent() {
            const s=surface()
            mousePress(s,8,280);mouseMove(s,8,240,30);mouseMove(s,8,160,50);mouseMove(s,8,100,30);mouseRelease(s,8,100)
            verify(scroller.contentY>20)
            compare(root.edits.length,0)
        }
        function test_escape_rolls_back_live_drag() {
            const s=surface(),p=pad.pointAt(0,0.5),q=pad.pointAt(0,0.85)
            mousePress(s,p.x,p.y);mouseMove(s,q.x,q.y);wait(70)
            verify(root.params.darken>1.5)
            keyClick(Qt.Key_Escape);mouseRelease(s,q.x,q.y)
            compare(root.params.darken,1)
        }
        function test_layer_change_discards_old_gesture() {
            const s=surface(),p=pad.pointAt(0,0.5),q=pad.pointAt(0,0.8)
            mousePress(s,p.x,p.y);mouseMove(s,q.x,q.y)
            pad.identity="layer-2";root.params={darken:0.2,brighten:0.3,color:0.4,luminance:0.5};root.edits=[]
            wait(80);mouseRelease(s,q.x,q.y)
            compare(root.edits.length,0);compare(root.params.darken,0.2)
            compare(pad.dragging,false)
        }
        function test_disabled_and_auto_are_read_only() {
            const s=surface(),p=pad.pointAt(0,0.8)
            pad.editable=false;mouseClick(s,p.x,p.y);wait(70);compare(root.edits.length,0)
            pad.editable=true;pad.enabled=false;mouseClick(s,p.x,p.y);wait(70);compare(root.edits.length,0)
        }
        function test_numeric_drag_cannot_write_to_new_layer() {
            const slider=findChild(pad,"test-web-value")
            mousePress(slider,25,slider.height/2);mouseMove(slider,65,slider.height/2);wait(15)
            verify(slider.pressed)
            pad.identity="layer-2";root.params={darken:0.2,brighten:1,color:1,luminance:1};root.edits=[]
            wait(100);mouseRelease(slider,65,slider.height/2);wait(20)
            compare(root.edits.length,0);compare(root.params.darken,0.2)
        }
        function test_center_points_can_be_selected_by_label() {
            root.params={darken:0,brighten:0,color:0,luminance:0}
            const label=findChild(pad,"test-web-axis-color")
            mouseClick(label,label.width/2,label.height/2)
            compare(pad.selectedAxis,2)
            surface().forceActiveFocus();keyClick(Qt.Key_Up)
            verify(root.params.color>0);compare(root.params.darken,0);compare(root.params.brighten,0)
        }
        function test_numeric_input_clamps_and_binding_survives() {
            const slider=findChild(pad,"test-web-value")
            slider.openEditor();wait(20)
            const input=findChild(slider,"vslider-editor-input");input.text="3.5"
            keyClick(Qt.Key_Return);compare(root.params.darken,2)
            root.params={darken:0.25,brighten:1,color:1,luminance:1};wait(20)
            compare(slider.value,0.25)
            pad.setSelected(-4);compare(root.params.darken,0)
        }
        function test_reset_and_undo() {
            pad.setSelected(0.4)
            const undo=findChild(pad,"test-web-undo");verify(undo.enabled)
            mouseClick(undo,undo.width/2,undo.height/2);compare(root.params.darken,1)
            pad.setSelected(1.8)
            verify(waitForPolish(root.Window.window))
            const reset=findChild(findChild(pad,"test-web-value"),"vslider-reset")
            verify(reset.enabled)
            mouseClick(reset,reset.width/2,reset.height/2);compare(root.params.darken,1)
        }
        function test_layout_at_narrow_and_wide_sizes() {
            for(const width of [280,330,500]) {
                pad.width=width;wait(10)
                for(const a of pad.axes) {
                    const label=findChild(pad,"test-web-axis-"+a.key)
                    verify(label.x>=0 && label.x+label.width<=pad.width+1)
                    verify(label.y>=0 && label.y+label.height<=surface().height+1)
                }
            }
            pad.width=400
        }
        function test_nine_axes_remain_selectable_and_labels_do_not_overlap() {
            const saved=pad.axes
            const names=["Hue protection","Chroma protection","Neutral protection","Original color","Luminance keep","Shadow protection","Highlight protection","Local compression","Temporal stability"]
            try {
                pad.axes=names.map((label,i)=>({key:"axis"+i,label:label,min:0,max:1,defaultValue:0.5}))
                for(const width of [240,280,330,500]) {
                    pad.width=width;verify(waitForPolish(root.Window.window));wait(10)
                    const labels=pad.axes.map(a=>findChild(pad,"test-web-axis-"+a.key))
                    for(let i=0;i<9;++i) {
                        const label=labels[i]
                        verify(label.x>=0&&label.x+label.width<=pad.width+1)
                        verify(label.y>=0&&label.y+label.height<=surface().height+1)
                        for(let j=i+1;j<9;++j){
                            const other=labels[j]
                            const overlapX=Math.min(label.x+label.width,other.x+other.width)-Math.max(label.x,other.x)
                            const overlapY=Math.min(label.y+label.height,other.y+other.height)-Math.max(label.y,other.y)
                            verify(overlapX<=1||overlapY<=1,"labels overlap at width "+width+": "+i+","+j)
                        }
                    }
                }
                pad.width=400;verify(waitForPolish(root.Window.window))
                for(let i=0;i<9;++i){
                    const label=findChild(pad,"test-web-axis-axis"+i)
                    mouseClick(label,label.width/2,label.height/2)
                    compare(pad.selectedAxis,i)
                    pad.setSelected((i+1)/10)
                    fuzzyCompare(root.params["axis"+i],(i+1)/10,0.00001)
                }
            } finally {pad.axes=saved;pad.width=400}
        }
    }
}
