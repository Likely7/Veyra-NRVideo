#pragma once
#include "veyra/engine/ExportQueue.h"
#include <QAbstractListModel>
#include <QStringList>

namespace veyra::ui {
class QmlExportQueueModel final:public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(qulonglong selectedId READ selectedId WRITE setSelectedId NOTIFY changed)
public:
    enum Role {ItemId=Qt::UserRole+1,Name,Input,Output,State,Progress,Note,Duration,Current,Locked,Inspecting};
    explicit QmlExportQueueModel(engine::ExportJobManager& worker,QObject* parent=nullptr);
    int rowCount(const QModelIndex& parent={})const override;
    QVariant data(const QModelIndex& index,int role)const override;
    QHash<int,QByteArray> roleNames()const override;
    int count()const{return rowCount();}
    qulonglong selectedId()const{return selected_;}
    void setSelectedId(qulonglong id);
    Q_INVOKABLE qulonglong addFile(const QString& path,const QString& output={});
    Q_INVOKABLE bool removeItem(qulonglong id);
    Q_INVOKABLE bool moveItem(qulonglong id,int destination);
    Q_INVOKABLE bool retryItem(qulonglong id);
    Q_INVOKABLE void clearWaiting();
    Q_INVOKABLE QVariantMap get(int index)const;
    bool tick();void refresh();
    engine::ExportQueue& queue(){return queue_;}
    const engine::ExportQueue& queue()const{return queue_;}
signals:
    void changed();
private:
    engine::ExportQueue queue_;
    uint64_t selected_=0;
};
}
