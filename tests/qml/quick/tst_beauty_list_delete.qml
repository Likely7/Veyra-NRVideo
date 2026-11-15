import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

Item {
    id: fixture
    width: 1280; height: 1800
    QtObject {
        id: backend
        property var beautyLayers: []
        property var removed: []
        property bool reject: false
        readonly property bool beautyAnyEnabled: beautyLayers.some(row=>row.enabled)
        property var beautyDetection: ({quality:1,maxFaces:4,stability:.5})
        property string beautyStatus: ""
        property var nodeTimings: ({})
        property var preferences: ({})
        property var nrLayers: []
        property var nrParams: ({})
        property var antiAliasLayers: []
        property var effectCatalog: []
        property var chain: []
        property var stageTimings: []
        property var fgBackendChoices: []
        property var gpuMonitorChoices: []
        property var colourParams: ({})
        property var protectionState: ({enabled:false,regions:[],feather:2})
        property var exclusionState: ({enabled:false,regions:[],feather:2})
        property var sourceMetadata: ({})
        property string sourceTitle: ""
        property string sourceKind: "file"
        property string outputText: ""
        property string chainError: ""
        property string fgBackendName: "dlss"
        property string fgProviderText: ""
        property string nrReuseStatus: ""
        property string performanceNotice: ""
        property int nrReuseMode: 0
        property int nodeMode: 0
        property bool chainValid: true
        property bool sourceActive: false
        property bool fgEnabled: false
        function layer(i) { return {index:4+i,layer:i,enabled:true,summary:"Layer "+i,
            present:true,restore:false,smooth:false,restoreStrength:1,restoreScope:0,regionExpand:.05,
            feather:.08,strength:1,smoothAmount:.5,radius:3.5,smoothDetail:.8,blend:.5,maskProtection:.5} }
        function reset(count) { let rows=[];for(let i=0;i<count;++i)rows.push(layer(i));beautyLayers=rows;removed=[];reject=false }
        function removeEffect(index) {
            removed=removed.concat([index])
            if(reject)return false
            beautyLayers=beautyLayers.filter(row=>row.index!==index)
            return true
        }
        function effectAvailability(id) { return {available:true,reason:""} }
        function observeHdrCoverage(on) {}
        function colourMetadata(id) { return {} }
        function selectedNrLayerIndex() { return -1 }
        function effectParameters(index) { return [] }
        function formatTime(t) { return "0:00" }
    }
    ProPage {
        id: page
        property var veyra: backend
        parent: fixture;width: fixture.width;height: fixture.height;visible:true;enabled:true
        tab: "quality"
    }
    TestCase {
        name: "BeautyListDelete"
        when: windowShown
        property bool oldMotion: false
        function initTestCase() { oldMotion=Theme.reduced;Theme.reduced=false;wait(80) }
        function cleanupTestCase() { Theme.reduced=oldMotion }
        function init() {
            backend.reset(2)
            findChild(page,"list-beauty-stage").open=true
            wait(650)
        }
        function test_menu_delete_does_not_hide_reused_card() {
            backend.reset(3);wait(60)
            const card=findChild(page,"beauty-card-2"),menu=findChild(page,"beauty-layer-menu")
            verify(card);verify(menu)
            menu.layer=card;menu.picked(4,{act:"del"})
            tryVerify(()=>backend.beautyLayers.length===2,1000)
            wait(400)
            const remaining=findChild(page,"beauty-card-2")
            verify(remaining);compare(remaining.layerData.index,6)
            fuzzyCompare(remaining.opacity,1,.001)
            compare(backend.removed[0],5)
        }
        function test_direct_delete_can_remove_last_layer_data() {
            return [{tag:"enabled",enabled:true},{tag:"disabled",enabled:false}]
        }
        function test_direct_delete_can_remove_last_layer(data) {
            backend.reset(1);wait(50)
            backend.beautyLayers=[Object.assign({},backend.beautyLayers[0],{enabled:data.enabled})]
            const button=findChild(page,"beauty-card-delete-1")
            verify(button);verify(button.visible);verify(button.width>0)
            mouseClick(button)
            compare(backend.beautyLayers.length,0)
            compare(backend.removed[0],4)
        }
        function test_rejected_delete_keeps_card_visible() {
            backend.reject=true
            const menu=findChild(page,"beauty-layer-menu")
            menu.layer=findChild(page,"beauty-card-1");menu.picked(4,{act:"del"})
            wait(400)
            compare(backend.beautyLayers.length,2)
            fuzzyCompare(findChild(page,"beauty-card-1").opacity,1,.001)
        }
    }
}
