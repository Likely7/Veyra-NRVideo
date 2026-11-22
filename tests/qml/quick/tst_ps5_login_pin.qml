import QtQuick
import QtTest
import Veyra

Item {
    width: 800; height: 600
    QtObject {
        id: mock
        property var ps5LoginPrompt: ({active:false,required:false,canSubmit:false,status:0,message:"",requestId:""})
        property bool accept: true
        property int submits: 0
        property int cancels: 0
        property string lastKey: ""
        property string lastPin: ""
        function ps5SubmitLoginPin(pin,key) { ++submits;lastPin=pin;lastKey=key;return accept }
        function ps5CancelLoginPin(key) { ++cancels;lastKey=key;ps5LoginPrompt={active:false,required:false};return true }
    }
    Item {
        id: host; anchors.fill: parent
        property string dialog: ""
        property real motionScale: 1
        function open(key) { dialog=key }
        function close() { panel.cancel() }
    }
    Ps5LoginPinDialog { id: panel; dialogHost: host; backend: mock }
    TestCase {
        name: "Ps5LoginPinFlow"; when: windowShown
        function challenge(key="session1:1",status=1) {
            mock.ps5LoginPrompt={active:true,required:true,canSubmit:true,status:status,requestId:key,message:status===4?"PIN incorrect":"Enter PIN"}
        }
        function field() { return findChild(panel,"ps5-login-pin-input") }
        function button() { return findChild(panel,"ps5-login-pin-confirm") }
        function init() {
            Theme.reduced=true;mock.ps5LoginPrompt={active:false,required:false};host.dialog=""
            mock.accept=true;mock.submits=0;mock.cancels=0;mock.lastPin="";mock.lastKey="";wait(5)
        }
        function test_idle_never_prompts() { verify(!panel.shown);compare(host.dialog,"");compare(mock.submits,0) }
        function test_challenge_opens_and_focuses_without_settings() {
            challenge();compare(host.dialog,"ps5-pin");verify(panel.shown)
            tryCompare(field(),"activeFocus",true);compare(field().echoMode,TextInput.Password)
            field().text="123";verify(!button().enabled)
        }
        function test_submit_continues_same_connection_and_waits() {
            challenge();field().text="1234";mouseClick(button())
            compare(mock.submits,1);compare(mock.lastPin,"1234");compare(mock.lastKey,"session1:1")
            compare(field().text,"");verify(!button().enabled);verify(panel.shown)
            mock.ps5LoginPrompt={active:true,required:true,canSubmit:false,status:3,requestId:"session1:1",message:"Waiting"}
            verify(panel.shown)
            mock.ps5LoginPrompt={active:false,required:false,status:5};verify(!panel.shown);compare(mock.cancels,0)
        }
        function test_incorrect_pin_can_retry_new_challenge() {
            challenge();field().text="1234";mouseClick(button())
            challenge("session1:2",4);verify(panel.shown);verify(!panel.submitted)
            field().text="5678";mouseClick(button());compare(mock.submits,2);compare(mock.lastKey,"session1:2")
        }
        function test_failed_queue_retains_input() {
            challenge();mock.accept=false;field().text="1234";mouseClick(button())
            compare(field().text,"1234");verify(panel.localError.length>0);verify(button().enabled)
        }
        function test_cancel_stops_pending_connection_and_clears_pin() {
            challenge();field().text="1234";mouseClick(findChild(panel,"ps5-login-pin-cancel"))
            compare(mock.cancels,1);compare(mock.lastKey,"session1:1");compare(field().text,"");verify(!panel.shown)
        }
        function test_session_end_discards_prompt_without_canceling_another_session() {
            challenge();field().text="1234";mock.ps5LoginPrompt={active:false,required:false}
            verify(!panel.shown);compare(field().text,"");compare(mock.cancels,0)
            challenge("session2:1");compare(field().text,"");compare(panel.requestId,"session2:1")
        }
    }
}
