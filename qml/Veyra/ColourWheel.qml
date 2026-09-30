import QtQuick
import QtQuick.Layouts

// 1.4.4 contract: clockwise hue, saturation as radius; separate -100..100 L.
ColumnLayout {
    id: root
    property string title: ""
    property real hue: 0
    property real saturation: 0
    property real luminance: 0
    signal edited(real hue, real saturation, real luminance)
    spacing: 6
    Text { Layout.alignment:Qt.AlignHCenter; text:root.title; color:Theme.t2; font.pixelSize:12 }
    Item {
        Layout.alignment:Qt.AlignHCenter
        Layout.preferredWidth:Math.min(root.width,132)
        Layout.preferredHeight:width
        Canvas {
            id: disc
            objectName: "colour-wheel-disc"
            anchors.fill:parent; anchors.margins:7
            onWidthChanged:requestPaint()
            onHeightChanged:requestPaint()
            onPaint: {
                const c=getContext("2d");c.reset()
                const r=width/2,cx=width/2,cy=height/2
                for(let i=0;i<360;++i){
                    c.beginPath();c.moveTo(cx,cy);c.arc(cx,cy,r,i*Math.PI/180,(i+1.5)*Math.PI/180);c.closePath()
                    c.fillStyle=Qt.hsla(i/360,1,0.5,1);c.fill()
                }
                const g=c.createRadialGradient(cx,cy,0,cx,cy,r)
                g.addColorStop(0,"#D3D4D9");g.addColorStop(1,"rgba(211,212,217,0)")
                c.fillStyle=g;c.beginPath();c.arc(cx,cy,r,0,Math.PI*2);c.fill()
            }
            Rectangle {
                width:10;height:10;radius:5;color:"#F7F7FA";border.width:2;border.color:"#17191E"
                x:disc.width/2+Math.cos(root.hue*Math.PI/180)*root.saturation/100*disc.width/2-width/2
                y:disc.height/2+Math.sin(root.hue*Math.PI/180)*root.saturation/100*disc.height/2-height/2
            }
            MouseArea {
                anchors.fill:parent;preventStealing:true
                function updateWheel(x,y){
                    const dx=(x-width/2)/(width/2),dy=(y-height/2)/(height/2)
                    const s=Math.min(100,Math.hypot(dx,dy)*100)
                    const h=s<0.001?root.hue:(Math.atan2(dy,dx)*180/Math.PI+360)%360
                    root.edited(h,s,root.luminance)
                }
                onPressed: mouse=>updateWheel(mouse.x,mouse.y)
                onPositionChanged: mouse=>{if(pressed)updateWheel(mouse.x,mouse.y)}
                onDoubleClicked:root.edited(0,0,0)
            }
        }
    }
    VSlider {
        objectName:"colour-wheel-luminance"
        Layout.fillWidth:true;from:-100;to:100;center:true;value:root.luminance
        onMoved:value=>root.edited(root.hue,root.saturation,Math.round(value))
    }
    Text {
        Layout.alignment:Qt.AlignHCenter
        text:"H "+Math.round(root.hue)+"° · S "+Math.round(root.saturation)+" · L "+Math.round(root.luminance)
        color:Theme.t3;font.pixelSize:10
    }
}
