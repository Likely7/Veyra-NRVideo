import QtQuick
import QtQuick.Controls
import QtTest
import Veyra
import VeyraTest 1.0

Rectangle {
    id: fixture
    width: 1280; height: 720
    color: Theme.bgOuter
    ListModel {
        id: queue
        property double selectedId: 0
        function moveItem(id, destination) { for(let i=0;i<count;++i)if(get(i).itemId===id){move(i,destination,1);return true} return false }
        function removeItem(id) { for(let i=0;i<count;++i)if(get(i).itemId===id){remove(i);return true}return false }
        function clearWaiting(){clear()}
        function retryItem(id){}
    }
    QtObject {
        id: backend
        property var exportQueueModel: queue
        property bool hasSource: false
        property bool isCapture: false
        property bool exportRunning: false
        property bool exportPaused: false
        property bool exportHevc: false
        property bool exportCompletionSound: true
        property bool exportSelectionEditable: true
        property int exportContainer: 0
        property int exportSrTargetIndex: -1
        property int exportRateControl: 2
        property int exportBitrateMbps: 24
        property int exportReadyCount: queue.count
        property int exportQueueCount: 0
        property int exportEncoded: 0
        property int exportAudioPolicy: 1
        property int exportSubtitlePolicy: 1
        property real exportProgress: 0
        property real exportTrimStart: 0
        property real exportTrimEnd: 0
        property string exportPresetName: "当前播放设置"
        property string exportTarget: "E:/output"
        property string exportStatus: "添加文件后，点击开始导出"
        property var exportTracks: []
        property var presetChoices: [{id: "-1",label: "当前播放设置"}]
        property bool imageBatchActive: false
        property string imageBatchStatus: ""
        property int compareMode: 0
        property real duration: 0
        property real position: 0
        property string positionText: "0:00"
        property string statusText: "选择左侧文件预览"
        property bool running: false
        property bool paused: false
        property int thumbnailGeneration: 0
        property int starts: 0
        property int additions: 0
        function fileThumbnailId(path, seconds){return ""}
        function formatTime(t){return Number(t).toFixed(1)}
        function previewExportItem(id){queue.selectedId=id}
        function addExportFilesDialog(){++additions}
        function addExportFiles(paths){++additions}
        function startExport(){++starts}
        function togglePlayPause(){}
        function setExportTrackPolicy(audio,policy){}
    }
    // VPage takes its parent and size from Main.qml's page stack (home) and only joins
    // the scene while it is the shown page; the fixture places it directly instead.
    ExportPage { id: page; property var veyra: backend; pageId: "exp"; parent: fixture; width: fixture.width; height: fixture.height; visible: true; enabled: true }
    TestCase {
        name: "ExportPage"
        when: windowShown
        function init(){
            fixture.width=1280;fixture.height=720;page.compactTab=0
            fixture.Window.window.width=1280;fixture.Window.window.height=720
            queue.clear();queue.selectedId=0;backend.starts=0;backend.additions=0
            for(let i=0;i<20;++i)queue.append({itemId:i+1,name:"clip-"+i+".mkv",input:"clip-"+i,output:"",state:"ready",note:"",duration:120,progress:0,current:false,locked:false,inspecting:false})
            waitForPolish(fixture.Window.window)
        }
        function inside(item, outer){
            const p=item.mapToItem(outer,0,0)
            verify(p.x>=-0.5&&p.y>=-0.5,item.objectName+" starts outside")
            verify(p.x+item.width<=outer.width+0.5&&p.y+item.height<=outer.height+0.5,item.objectName+" ends outside "+p.x+","+p.y+" "+item.width+","+item.height)
        }
        function test_layout_data(){return [{tag:"minimum",w:720,h:260},{tag:"720p",w:1280,h:720},{tag:"laptop",w:1366,h:768},{tag:"1080p",w:1920,h:1080}]}
        function test_layout(data){
            fixture.width=data.w;fixture.height=data.h
            fixture.Window.window.width=data.w;fixture.Window.window.height=data.h
            waitForPolish(fixture.Window.window)
            for(let tab=0;tab<3;++tab){
                page.compactTab=tab;waitForPolish(fixture.Window.window)
                for(const name of ["export-queue-card","export-settings-card","export-preview-card","export-trim-card","export-footer"]){const item=findChild(page,name);if(item.visible)inside(item,page)}
                inside(findChild(page,"export-start"),page)
                if(tab===2||!page.compact){
                    const select=findChild(page,"export-resolution")
                    const scroll=findChild(page,"export-settings-scroll")
                    const point=select.mapToItem(scroll,0,0)
                    verify(point.x>=0&&point.x+select.width<=scroll.width+1,"resolution must fit horizontally")
                }
            }
        }
        function test_queue_actions(){
            mouseClick(findChild(page,"export-add-files"));compare(backend.additions,1);compare(backend.starts,0)
            const row=findChild(page,"export-queue-row-1");verify(row!==null)
            mouseClick(row,100,20);tryCompare(queue,"selectedId",2)
            mouseClick(findChild(page,"export-remove-0"));compare(queue.count,19);compare(queue.get(0).itemId,2)
            const handle=findChild(page,"export-drag-0")
            mouseDrag(handle,8,20,0,156,Qt.LeftButton,0,200)
            compare(queue.get(2).itemId,2)
            mouseClick(findChild(page,"export-start"));compare(backend.starts,1)
            mouseClick(findChild(page,"export-clear-queue"));compare(queue.count,0)
        }
        function test_resolution_menu_fits_short_window(){
            fixture.width=720;fixture.height=260;page.compactTab=2
            fixture.Window.window.width=720;fixture.Window.window.height=260
            waitForPolish(fixture.Window.window)
            const scroll=findChild(page,"export-settings-scroll")
            const select=findChild(page,"export-resolution")
            scroll.contentItem.contentY=select.mapToItem(scroll.contentItem.contentItem,0,0).y
            waitForPolish(fixture.Window.window)
            mouseClick(select,select.width/2,select.height/2)
            tryCompare(select.popup,"visible",true)
            verify(select.popup.y>=0&&select.popup.y+select.popup.height<=fixture.Window.window.height)
            select.popup.close()
        }
        function cleanupTestCase(){
            if(!exportTestOutputDirectory)return
            fixture.width=1280;fixture.height=720;fixture.Window.window.width=1280;fixture.Window.window.height=720
            page.compactTab=0;waitForPolish(fixture.Window.window);wait(50)
            findChild(page,"export-settings-scroll").contentItem.contentY=0
            grabImage(fixture).save(exportTestOutputDirectory+"/export-1280.png")
            fixture.width=720;fixture.height=260;fixture.Window.window.width=720;fixture.Window.window.height=260
            page.compactTab=2;waitForPolish(fixture.Window.window);wait(50)
            grabImage(fixture).save(exportTestOutputDirectory+"/export-720-settings.png")
        }
    }
}
