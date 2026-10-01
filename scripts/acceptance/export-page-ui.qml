// Loaded only into a task-owned staging copy of Main.qml by the test runner.
// Drives the production bridge/model/worker; never changes installed user data.
import QtQuick
Item {
    id: test
    property string outputDirectory: ""
    property string evidenceDirectory: ""
    property var appWindow: Window.window
    property int step: 0
    property int ticks: 0
    property var ids: []
    property string media: "E:/项目/Veyra/tests/export-page-20261002/media/"
    function find(item,name){if(item.objectName===name)return item;for(const child of item.children||[]){const hit=find(child,name);if(hit)return hit}return null}
    function require(ok,why){if(!ok){console.error("EXPORT_UI_FAIL",step,why);timer.stop();Qt.quit();throw new Error(why)}}
    function done(){console.log("EXPORT_UI_PASS: production model, per-file trim, cancel/retry, frozen batch, mixed failure, sound enabled/disabled, image batch success/failure/cancel");timer.stop();Qt.quit()}
    Timer {
        id: timer
        interval: 200; repeat: true; running: true
        onTriggered: {
            if(++test.ticks>600){test.require(false,"120-second deadline");return}
            if(!test.outputDirectory.length)return
            const model=veyra.exportQueueModel
            if(test.step===0){
                if(test.ticks<5)return
                let files=[];for(let i=0;i<20;++i)files.push(test.media+"tracks.mp4")
                veyra.addExportFiles(files)
                test.require(model.count===20&&!veyra.exportRunning,"add-only contract")
                test.require(model.moveItem(model.get(19).itemId,0),"production model move")
                test.require(model.removeItem(model.get(3).itemId),"production model remove")
                model.clearWaiting();test.require(model.count===0,"production model clear")
                veyra.addExportFiles([test.media+"tracks.mp4",test.media+"font-ass.mkv",test.media+"tracks.mkv"])
                for(let j=0;j<3;++j)test.ids.push(model.get(j).itemId)
                veyra.exportContainer=1;veyra.exportSrTargetIndex=0
                veyra.exportCompletionSound=true
                veyra.previewExportItem(test.ids[0]);test.step=1
            }else if(test.step===1){
                if(!veyra.hasSource||veyra.duration<=0)return
                veyra.exportTrimStart=0.5;veyra.exportTrimEnd=1.5
                test.require(Math.abs(veyra.exportTrimStart-0.5)<0.01,"set first trim")
                veyra.previewExportItem(test.ids[1]);test.step=2
            }else if(test.step===2){
                if(!veyra.hasSource||veyra.duration<=0)return
                test.require(veyra.exportTrimStart===0&&veyra.exportTrimEnd===0,"trim leaked to second item")
                veyra.previewExportItem(test.ids[0])
                test.require(Math.abs(veyra.exportTrimStart-0.5)<0.01&&Math.abs(veyra.exportTrimEnd-1.5)<0.01,"trim not restored")
                model.moveItem(test.ids[2],0)
                veyra.startExport();veyra.cancelExport()
                test.require(veyra.exportReadyCount===3,"cancel lost waiting files")
                test.step=3
            }else if(test.step===3){
                if(veyra.exportRunning)return
                veyra.startExport()
                test.require(veyra.exportRunning,"restart after cancel")
                veyra.addExportFiles([test.media+"tracks.mp4"])
                veyra.previewExportItem(test.ids[1]);test.step=4
            }else if(test.step===4){
                if(veyra.exportRunning)return
                test.require(veyra.exportItems.filter(i=>i.state==="done").length===3,"first batch did not finish all members")
                test.require(veyra.exportReadyCount===1,"late item joined active batch")
                test.step=7
                test.appWindow.contentItem.grabToImage(result=>{
                    result.saveToFile(test.evidenceDirectory+"/production-export.png")
                    veyra.addExportFiles([test.media+"broken.mkv"])
                    veyra.startExport();test.step=5
                })
            }else if(test.step===5){
                if(veyra.exportRunning)return
                test.require(veyra.exportQueueFailures===1,"mixed batch failure missing")
                test.require(veyra.exportItems.some(i=>i.state==="failed"&&i.note.length>0),"specific failure reason missing")
                model.clearWaiting();veyra.exportCompletionSound=false
                veyra.addExportFiles([test.media+"tracks.mp4"]);veyra.startExport();test.step=6
            }else if(test.step===6){
                if(veyra.exportRunning)return
                test.require(veyra.exportItems.every(i=>i.state==="done"),"muted batch failed")
                test.require(!veyra.exportCompletionSound,"sound preference reverted")
                veyra.exportCompletionSound=true
                test.require(veyra.startImageBatch([test.media+"frame-a.png",test.media+"frame-b.png"],test.evidenceDirectory+"/images"),"image batch start refused")
                test.step=8
            }else if(test.step===8){
                if(veyra.imageBatchActive)return
                test.require(veyra.imageBatchDone===2&&veyra.imageBatchFailures===0,"image batch success missing")
                test.require(veyra.startImageBatch([test.media+"frame-a.png",test.media+"frame-b.png"],test.evidenceDirectory+"/images"),"image conflict test refused")
                test.step=9
            }else if(test.step===9){
                if(veyra.imageBatchActive)return
                test.require(veyra.imageBatchDone===0&&veyra.imageBatchFailures===2,"existing image outputs were overwritten")
                test.require(veyra.startImageBatch([test.media+"frame-a.png"],test.evidenceDirectory+"/cancel-images"),"image cancel test refused")
                veyra.cancelImageBatch()
                test.require(!veyra.imageBatchActive,"image cancellation failed")
                veyra.exportCompletionSound=false
                test.done()
            }
        }
    }
}
