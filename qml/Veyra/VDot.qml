import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: dot
    property bool warn: false
    property bool err: false
    property bool off: false
    width: 7; height: 7; radius: 999
    color: err ? Theme.err : warn ? Theme.warn : off ? Theme.t3 : Theme.ok
    Accessible.role: Accessible.Indicator
    Accessible.name: err ? qsTr("错误") : warn ? qsTr("警告") : off ? qsTr("离线") : qsTr("正常")
}
