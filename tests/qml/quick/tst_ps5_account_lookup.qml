import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

Item {
    id: fixture
    width: 900; height: 1500
    QtObject {
        id: psnBackend
        property bool busy: false
        property bool signedIn: false
        property bool awaitingRedirect: false
        property string status: ""
        property var consoles: [{id:"a".repeat(64),label:"My PS5",ps5:true},{id:"0".repeat(64),label:"Primary PS4",ps5:false}]
        property int logins: 0
        property int refreshes: 0
        property int signOuts: 0
        property string submittedUrl: ""
        function beginLogin() { ++logins; awaitingRedirect=true }
        function completeLogin(url) { submittedUrl=url; awaitingRedirect=false; signedIn=true }
        function refreshConsoles() { ++refreshes }
        function signOut() { ++signOuts; signedIn=false; awaitingRedirect=false }
        function cancel() { busy=false; awaitingRedirect=false }
    }
    QtObject {
        id: backend
        property var ps5: ({host:"192.168.1.2",account:"saved-id",profile:"saved.dat",busy:false,
            connection:"local",psnDuid:"",active:false,quality:3,codec:0,bitrate:2,decode:0,sampling:1,status:""})
        property var psn: psnBackend
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
        function init() {
            backend.lookups=0;backend.ps5Set("account","saved-id");backend.ps5Set("busy",false)
            backend.ps5Set("connection","local");backend.ps5Set("codec",0);backend.ps5Set("psnDuid","")
            psnBackend.busy=false;psnBackend.signedIn=false;psnBackend.awaitingRedirect=false
            psnBackend.logins=0;psnBackend.refreshes=0;psnBackend.signOuts=0;psnBackend.submittedUrl=""
        }
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
        function test_modes_preserve_local_pairing_and_show_psn() {
            const mode=findChild(panel,"ps5-connection-mode")
            const lookup=findChild(panel,"ps5-account-lookup")
            const login=findChild(panel,"psn-login")
            verify(lookup.visible);verify(!login.visible)
            mode.picked("psn")
            compare(backend.ps5.connection,"psn")
            verify(!lookup.visible);verify(login.visible)
            compare(backend.ps5.profile,"saved.dat")
            mode.picked("local")
            verify(lookup.visible);compare(backend.ps5.account,"saved-id")
        }
        function test_psn_callback_cleared_after_submit() {
            backend.ps5Set("connection","psn")
            findChild(panel,"psn-login").clicked()
            compare(psnBackend.logins,1);verify(psnBackend.awaitingRedirect)
            const field=findChild(panel,"psn-callback-url")
            field.text="https://remoteplay.dl.playstation.net/remoteplay/redirect?code=test-only"
            findChild(panel,"psn-submit-login").clicked()
            compare(field.text,"")
            verify(psnBackend.submittedUrl.endsWith("code=test-only"))
            verify(psnBackend.signedIn)
            findChild(panel,"psn-refresh").clicked()
            compare(psnBackend.refreshes,1)
            findChild(panel,"psn-sign-out").clicked()
            compare(psnBackend.signOuts,1);verify(!psnBackend.signedIn)
        }
        function test_ps4_forces_h264_selector_without_local_pairing() {
            backend.ps5Set("connection","psn");backend.ps5Set("codec",2)
            psnBackend.signedIn=true
            findChild(panel,"psn-console").picked("0".repeat(64))
            const codec=findChild(panel,"ps5-codec")
            compare(codec.current,"0");verify(!codec.enabled)
            findChild(panel,"psn-console").picked("a".repeat(64))
            compare(codec.current,"2");verify(codec.enabled)
            compare(backend.ps5.profile,"saved.dat")
        }
        function test_settings_have_no_manual_login_pin() {
            compare(findChild(panel,"ps5-login-pin"),null)
            compare(findChild(panel,"ps5-submit-pin"),null)
        }
    }
}
