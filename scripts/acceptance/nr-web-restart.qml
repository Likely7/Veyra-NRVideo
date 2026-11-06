import QtQuick
Item {
    id:test
    property string media:""
    property string shot:""
    Timer {
        interval:500;running:true;repeat:false
        onTriggered:{try{
            const n=veyra.nrLayers[0]
            if(veyra.preferences.nrWebEditor!==true||!n||n.sizePercent!==62||Math.abs(n.darken-0.65)>0.0001||
                Math.abs(n.colorRetention-0.6)>0.0001||n.correctionAuto!==false)throw new Error("saved editor/NR values not restored")
            veyra.logUi("nr-web-test","PASS editor preference and original NR settings restored after restart")
            veyra.logUi("nr-web-test","NR_WEB_UI_PASS");console.log("NR_WEB_UI_PASS");Qt.quit()
        }catch(e){veyra.logUi("nr-web-test","NR_WEB_UI_FAIL "+e.message);console.log("NR_WEB_UI_FAIL "+e.message);Qt.quit()}}
    }
}
