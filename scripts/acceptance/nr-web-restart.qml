import QtQuick
Item {
    id:test
    property string media:""
    property string shot:""
    Timer {
        interval:500;running:true;repeat:false
        onTriggered:{try{
            const n=veyra.nrLayers[0]
            if(veyra.preferences.nrResidualView!=="web"||veyra.preferences.nrCorrectionView!=="web"||!n||n.sizePercent!==62||
                Math.abs(n.hueProtection-0.1)>0.0001||Math.abs(n.chromaProtection-0.2)>0.0001||Math.abs(n.neutralProtection-0.3)>0.0001||
                Math.abs(n.darken-0.65)>0.0001||n.correctionAuto!==false)throw new Error("group views or original NR values not restored")
            veyra.logUi("nr-web-test","PASS both group selections and all original NR settings restored after restart")
            veyra.logUi("nr-web-test","NR_WEB_UI_PASS");console.log("NR_WEB_UI_PASS");Qt.quit()
        }catch(e){veyra.logUi("nr-web-test","NR_WEB_UI_FAIL "+e.message);console.log("NR_WEB_UI_FAIL "+e.message);Qt.quit()}}
    }
}
