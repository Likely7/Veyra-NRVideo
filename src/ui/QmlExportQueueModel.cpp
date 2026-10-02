#include "veyra/ui/QmlExportQueueModel.h"
#include <QFileInfo>
#include "veyra/ui/UiLanguage.h"

namespace veyra::ui {
QmlExportQueueModel::QmlExportQueueModel(engine::ExportJobManager& worker,QObject* parent):QAbstractListModel(parent),queue_(worker){}
int QmlExportQueueModel::rowCount(const QModelIndex& parent)const{return parent.isValid()?0:int(queue_.items().size());}
QHash<int,QByteArray> QmlExportQueueModel::roleNames()const{return {{ItemId,"itemId"},{Name,"name"},{Input,"input"},{Output,"output"},{State,"state"},{Progress,"progress"},{Note,"note"},{Duration,"duration"},{Current,"current"},{Locked,"locked"},{Inspecting,"inspecting"}};}
QVariant QmlExportQueueModel::data(const QModelIndex& index,int role)const{
    if(!index.isValid()||index.row()<0||index.row()>=rowCount())return {};
    const auto& item=queue_.items()[index.row()];
    using S=engine::ExportItemState;
    switch(role){
    case ItemId:return QVariant::fromValue(qulonglong(item.id));
    case Name:return QFileInfo(QString::fromStdWString(item.input)).fileName();
    case Input:return QString::fromStdWString(item.input);
    case Output:return QString::fromStdWString(item.output);
    case State:switch(item.state){case S::Ready:return "ready";case S::Queued:return "queued";case S::Running:return "running";case S::Done:return "done";case S::Failed:return "failed";case S::Cancelled:return "cancelled";}break;
    case Progress:return item.progress;
    case Note:return veyra::ui::i18n::text(item.note);
    case Duration:return item.info.duration;
    case Current:return item.id==selected_;
    case Locked:return item.state==S::Running;
    case Inspecting:return item.inspected!=item.revision;
    }
    return {};
}
QVariantMap QmlExportQueueModel::get(int row)const{
    QVariantMap result;if(row<0||row>=rowCount())return result;
    const auto roles=roleNames();for(auto it=roles.cbegin();it!=roles.cend();++it)result[QString::fromUtf8(it.value())]=data(index(row),it.key());return result;
}
void QmlExportQueueModel::refresh(){if(rowCount())emit dataChanged(index(0),index(rowCount()-1));emit changed();}
void QmlExportQueueModel::setSelectedId(qulonglong id){if(selected_==id||(id&&!queue_.find(id)))return;selected_=id;refresh();}
qulonglong QmlExportQueueModel::addFile(const QString& path,const QString& output){
    beginInsertRows({},rowCount(),rowCount());const auto id=queue_.add(path.toStdWString(),output.toStdWString());endInsertRows();emit changed();return id;
}
bool QmlExportQueueModel::removeItem(qulonglong id){
    for(int row=0;row<rowCount();++row)if(queue_.items()[row].id==id){
        if(queue_.items()[row].state==engine::ExportItemState::Running)return false;
        beginRemoveRows({},row,row);queue_.remove(id);endRemoveRows();if(selected_==id)selected_=0;emit changed();return true;
    }
    return false;
}
bool QmlExportQueueModel::moveItem(qulonglong id,int destination){
    if(destination<0||destination>=rowCount())return false;
    for(int row=0;row<rowCount();++row)if(queue_.items()[row].id==id){
        if(row==destination)return true;if(queue_.items()[row].state==engine::ExportItemState::Running)return false;
        if(!beginMoveRows({},row,row,{},destination>row?destination+1:destination))return false;
        const bool result=queue_.move(id,destination);endMoveRows();emit changed();return result;
    }
    return false;
}
bool QmlExportQueueModel::retryItem(qulonglong id){const bool result=queue_.retry(id);if(result)refresh();return result;}
void QmlExportQueueModel::clearWaiting(){
    using S=engine::ExportItemState;
    const bool busy=queue_.busy();
    for(int i=rowCount()-1;i>=0;--i){const auto& item=queue_.items()[i];if(!busy||item.state==S::Ready||item.state==S::Queued||item.state==S::Cancelled)removeItem(item.id);}
}
bool QmlExportQueueModel::tick(){const bool changed=queue_.tick();if(changed)refresh();return changed;}
}
