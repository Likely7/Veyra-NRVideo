import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import Veyra

Item {
    id: root
    width: 900; height: 600
    Item { id: anchor; x: root.width - 24; y: root.height - 24; width: 1; height: 1 }
    VMenu { id: menu; title: "Long context menu" }
    VSelect {
        id: warningSelect; x: 12; y: 220; width: 250
        value: "MJPEG（不建议使用）"
        options: [{id:"nv12",label:"NV12"},{id:"jpeg",label:"MJPEG（不建议使用）",danger:true}]
    }
    VRow {
        id: row; x: 12; y: 12; width: 285
        label: "The capture device's actual supported pixel format"
        hint: "This information must stay readable even in a narrow panel."
        VSelect { implicitWidth: 220; value: "3840 × 2160 @ 59.94 fps · P010 · HDR · Native · Low latency" }
    }
    function labels(item, output) {
        if (item.objectName === "vmenu-option-label") output.push(item)
        for (const child of item.children || []) labels(child, output)
        return output
    }
    TestCase {
        name: "MenuAndRowLayout"
        when: windowShown
        function cleanup() { menu.close() }
        function test_open_motion_preserves_text_layout_data() {
            return [{tag:"menu",select:false},{tag:"select",select:true}]
        }
        function test_open_motion_preserves_text_layout(data) {
            root.Window.window.width=900;root.Window.window.height=600
            root.width=900;root.height=600
            let popup=menu
            if(data.select) {
                for(const child of warningSelect.data)
                    if(child instanceof VMenu) popup=child
                mouseClick(warningSelect,warningSelect.width/2,warningSelect.height/2)
            } else {
                popup.items=[{label:"画面增强 · Enhancement"},{label:"第二项 · Second choice"}]
                popup.openAt(warningSelect,"down")
            }
            tryVerify(()=>popup.visible)
            const label=labels(popup.contentItem,[])[0]
            verify(label!==undefined)
            const start=label.mapToItem(popup.contentItem,0,0)
            const end=label.mapToItem(popup.contentItem,label.width,label.height)
            let sawScaleMotion=false
            for(let i=0;i<28;++i) {
                sawScaleMotion=sawScaleMotion || Math.abs(popup.motionScale-1)>0.001
                wait(16)
                const p=label.mapToItem(popup.contentItem,0,0),q=label.mapToItem(popup.contentItem,label.width,label.height)
                fuzzyCompare(p.x,start.x,0.05,"opening must not relayout text x")
                fuzzyCompare(p.y,start.y,0.05,"opening must not relayout text y")
                fuzzyCompare(q.x-p.x,end.x-start.x,0.05,"local text width must stay fixed")
                fuzzyCompare(q.y-p.y,end.y-start.y,0.05,"local line height must stay fixed")
            }
            verify(sawScaleMotion,"the approved spring scale animation must still run")
            console.log("MENU_LAYOUT_RENDERER software="+(root.GraphicsInfo.api===GraphicsInfo.Software))
            popup.close();tryCompare(popup,"visible",false)
        }
        function test_rendered_spring_text() {
            if(!exportTestOutputDirectory.length)skip("Raster comparison uses an explicit private artifact directory")
            root.Window.window.width=900;root.Window.window.height=600;root.width=900;root.height=600
            menu.items=[{label:"画面处理 Rendering 0123456789"},{label:"NR 内部分辨率 · Native resolution"}]
            menu.openAt(warningSelect,"down");tryCompare(menu,"opened",true);wait(30)
            const label=labels(menu.contentItem,[])[0]
            let motion=null
            for(const group of menu.enter.animations)
                for(const animation of group.animations || [])
                    if(animation.property==="s")motion=animation.target
            verify(motion!==null)
            // These frames draw the named scales, so keep software drawing off its hold.
            const keepHold=menu.holdLanding
            menu.holdLanding=false
            let frames=[]
            for(const scale of [1,0.9,0.95,0.99,1.02,1.04]) {
                motion.s=scale;motion.dy=0
                wait(40)
                const file=exportTestOutputDirectory+"/spring-"+scale+".png"
                verify(testWindowKeys.savePresented(root.Window.window,file))
                const p=label.mapToItem(root.Window.contentItem,0,0),q=label.mapToItem(root.Window.contentItem,label.width,label.height)
                frames.push({scale:scale,file:file,x:p.x,y:p.y,width:q.x-p.x,height:q.y-p.y})
            }
            console.log("MENU_RASTER_FRAMES "+JSON.stringify(frames))
            motion.s=1;motion.dy=0
            menu.holdLanding=keepHold
            menu.close();tryCompare(menu,"visible",false)
        }
        // Software drawing puts labels on whole pixels, so the spring's tail
        // around its end moved them 1 px sideways and back twice per opening.
        function test_open_spring_lands_once_data() {
            return [{tag:"software hold",hold:true},{tag:"full spring",hold:false}]
        }
        function test_open_spring_lands_once(data) {
            root.Window.window.width=900;root.Window.window.height=600
            root.width=900;root.height=600
            let popup=null
            for(const child of warningSelect.data)
                if(child instanceof VMenu) popup=child
            verify(popup!==null)
            const software=root.GraphicsInfo.api===GraphicsInfo.Software
            if(data.hold&&!software) skip("the hold belongs to software drawing")
            const keepHold=popup.holdLanding
            // The menu was shown by the earlier tests, so its binding knows the renderer.
            compare(keepHold,software,"the hold follows the renderer")
            // The software row keeps the menu's own binding, the other turns it off.
            if(!data.hold) popup.holdLanding=false
            mouseClick(warningSelect,warningSelect.width/2,warningSelect.height/2)
            tryVerify(()=>popup.visible)
            compare(popup.holdLanding,data.hold)
            const label=labels(popup.contentItem,[])[0]
            const scale=popup.contentItem.transform[0],lift=popup.contentItem.transform[1]
            // Wait for the transition's first tick (motion.s keeps the last value until then).
            for(let i=0;i<200&&popup.motionScale>=1;++i) wait(1)
            let frames=[]
            while(!popup.opened&&frames.length<600) {
                const p=label.mapToItem(null,0,0)
                frames.push({raw:popup.motionScale,s:scale.xScale,dy:lift.y,x:p.x,y:p.y})
                wait(2)
            }
            verify(popup.opened)
            const end=label.mapToItem(null,0,0)
            verify(frames.length>4,"sampled "+frames.length+" frames")
            verify(Math.max(...frames.map(f=>f.raw))>1.001,"the approved spring must still overshoot")
            if(data.hold) {
                let landed=false
                for(let i=0;i<frames.length;++i) {
                    const f=frames[i]
                    verify(f.s<=1&&f.dy>=0,"frame "+i+" went past the end: "+JSON.stringify(f))
                    if(i>0) {
                        const g=frames[i-1]
                        verify(Math.abs(f.x-end.x)<=Math.abs(g.x-end.x)+1e-6&&Math.abs(f.y-end.y)<=Math.abs(g.y-end.y)+1e-6,
                               "label moved away from its place: "+JSON.stringify([g,f,end]))
                    }
                    if(landed) {
                        compare(f.s,1);compare(f.dy,0)
                        fuzzyCompare(f.x,end.x,1e-6);fuzzyCompare(f.y,end.y,1e-6)
                    }
                    landed=landed||f.s===1
                }
                verify(frames.some(f=>f.s===1&&f.raw>1.001),"the hold must cover the spring's overshoot")
            } else {
                for(const f of frames)
                    compare(f.s,f.raw,"without the hold the drawn scale is the spring")
                verify(Math.max(...frames.map(f=>f.s))>1.001,"GPU drawing keeps the whole spring")
            }
            console.log("MENU_SPRING_LANDING "+data.tag+" frames="+frames.length+" software="+software)
            popup.close()
            let exitMin=1
            while(popup.visible&&exitMin>0.95) { exitMin=Math.min(exitMin,scale.xScale);wait(2) }
            verify(exitMin<=0.95,"closing must still scale the menu down: "+exitMin)
            tryCompare(popup,"visible",false)
            if(!data.hold) popup.holdLanding=keepHold
        }
        function test_mjpeg_warning() {
            compare(warningSelect.valueColor,Theme.err)
            warningSelect.value="NV12"
            compare(warningSelect.valueColor,Theme.t1)
            warningSelect.value="MJPEG（不建议使用）"
            menu.items=warningSelect.options
            menu.openAt(warningSelect,"at");tryCompare(menu,"opened",true)
            const items=labels(menu.contentItem,[])
            compare(items.length,2)
            compare(items[0].color,Theme.t1)
            compare(items[1].color,Theme.err)
            verify(!items[1].truncated)
        }
        function test_text_data() {
            return [{tag:"English",label:"Duplicate the selected enhancement node and reconnect its downstream output",width:900},
                    {tag:"Chinese",label:"3840 × 2160 @ 59.94 fps · P010 · 原生格式 · HDR · 低延迟 · 驱动默认输入",width:900},
                    {tag:"Japanese",label:"選択した画像補正ノードを複製して出力に再接続する",width:420},
                    {tag:"Narrow",label:"C:/A_very_long_folder_name/"+"unbroken".repeat(60),width:300}]
        }
        function test_text(data) {
            root.Window.window.width=data.width; root.Window.window.height=500
            root.width=data.width; root.height=500
            menu.items=[{label:data.label},{label:"Second choice"}]
            menu.openAt(anchor,"at")
            tryCompare(menu,"opened",true)
            wait(500)
            verify(menu.width<=data.width-16)
            verify(menu.x>=8 && menu.x+menu.width<=data.width-7, "x="+menu.x+" width="+menu.width+" window="+data.width)
            verify(menu.y>=8 && menu.y+menu.height<=500-7)
            const items=labels(menu.contentItem,[])
            compare(items.length,2)
            verify(!items[0].truncated)
            verify(items[0].contentWidth<=items[0].width+1)
            verify(items[0].contentHeight<=items[0].height+1)
            if(data.width===900) verify(menu.width>220)
        }
        function test_tall_menu_scrolls() {
            root.Window.window.width=600;root.Window.window.height=400;root.width=600;root.height=400
            let entries=[];for(let i=0;i<60;i++)entries.push({label:"Capture format "+i+" · 3840 × 2160 @ 59.94 fps · P010"})
            menu.items=entries;menu.openAt(anchor,"at");tryCompare(menu,"opened",true);wait(500)
            verify(menu.height<=384)
            const items=labels(menu.contentItem,[]);compare(items.length,60)
            verify(!items[59].truncated)
        }
        function test_narrow_row_reflows() {
            tryCompare(row,"stacked",true)
            verify(row.implicitHeight>60)
            verify(row.childrenRect.width<=row.width+1)
        }
    }
}
