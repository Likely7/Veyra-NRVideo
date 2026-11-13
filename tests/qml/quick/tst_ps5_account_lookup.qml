import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

Item {
    id: fixture
    width: 900; height: 1500
    QtObject {
        id: backend
        property var ps5: ({host:"192.168.1.2",account:"saved-id",profile:"saved.dat",busy:false,
            active:false,quality:3,codec:0,bitrate:2,decode:0,sampling:1,status:""})
        property var ps5Profiles: [{id:"saved.dat",label:"My PS5"}]
        property var ps5Control: ({available:false,enabled:false})
        property int lookups: 0
        function ps5Load() {}
        function ps5OpenAccountChecker() { ++lookups }
        function ps5Set(key,value) { const next=Object.assign({},ps5);next[key]=value;ps5=next }
    }
    Item {
        id: host
        anchors.fill: parent
        property string dialog: "ps5"
        property real motionScale: 1
        function close() {}
        function open(key) { dialog=key }
        function startPs5() {}
    }
    Ps5SourceDialog { id: panel; property var veyra: backend; dialogHost: host }
    TestCase {
        name: "Ps5AccountLookup"
        when: windowShown
        property bool oldMotion: false
        function initTestCase() { oldMotion=Theme.reduced;Theme.reduced=true;wait(60) }
        function cleanupTestCase() { Theme.reduced=oldMotion }
        function init() { backend.lookups=0;backend.ps5Set("account","saved-id");backend.ps5Set("busy",false) }
        function test_lookup_button_preserves_saved_console() {
            const button=findChild(panel,"ps5-account-lookup")
            verify(button);mouseClick(button)
            compare(backend.lookups,1)
            compare(backend.ps5.account,"saved-id")
            compare(backend.ps5.host,"192.168.1.2")
            compare(backend.ps5.profile,"saved.dat")
        }
        function test_pasted_id_uses_existing_pairing_field() {
            const field=findChild(panel,"ps5-account-id")
            verify(field);field.edited("123456789012345678")
            compare(backend.ps5.account,"123456789012345678")
            compare(backend.ps5.profile,"saved.dat")
        }
        function texts(item,result) {
            if(item.text!==undefined)result.push(String(item.text))
            for(const child of item.children)texts(child,result)
            return result
        }
        function test_obsolete_oauth_actions_are_absent() {
            const labels=texts(panel,[])
            verify(labels.indexOf("登录 PSN")<0)
            verify(labels.indexOf("提交登录结果")<0)
            verify(labels.indexOf("退出")<0)
            verify(labels.indexOf("PSN Account ID")>=0)
        }
    }
}
